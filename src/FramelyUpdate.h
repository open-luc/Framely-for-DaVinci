// FramelyUpdate.h — check GitHub Releases for a newer Framely and install it in place (macOS).
// No separate app: uses the system's curl, ditto and (only if the plugin folder isn't writable) an admin password prompt.
#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/stat.h>

namespace framely_update {

static const char* kRepo = "open-luc/Framely-for-DaVinci";
static const char* kAssetName = "Framely-macOS.zip";

inline std::string releasesPage() { return std::string("https://github.com/") + kRepo + "/releases"; }

// Run a shell command, capture stdout. Returns exit status (or -1).
inline int run(const std::string& cmd, std::string* out = nullptr) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return -1;
    char buf[4096]; std::string s; size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) s.append(buf, n);
    int st = pclose(p);
    if (out) *out = s;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
// Single-quote for /bin/sh.
inline std::string q(const std::string& s) {
    std::string r = "'";
    for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; }
    return r + "'";
}

// "0.1.10" > "0.1.9"; a leading "v" is ignored; a "-alpha" style suffix sorts before the plain version.
inline int compareVersions(std::string a, std::string b) {
    auto strip = [](std::string& v) { if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1); };
    strip(a); strip(b);
    auto split = [](const std::string& v, std::string& suffix) {
        std::vector<long> n; std::string core = v; auto dash = v.find('-');
        if (dash != std::string::npos) { core = v.substr(0, dash); suffix = v.substr(dash + 1); }
        std::stringstream ss(core); std::string part;
        while (std::getline(ss, part, '.')) n.push_back(strtol(part.c_str(), nullptr, 10));
        return n;
    };
    std::string sa, sb; auto na = split(a, sa), nb = split(b, sb);
    for (size_t i = 0; i < std::max(na.size(), nb.size()); i++) {
        long x = i < na.size() ? na[i] : 0, y = i < nb.size() ? nb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    if (sa == sb) return 0;
    if (sa.empty()) return 1;     // 0.2.0 > 0.2.0-alpha
    if (sb.empty()) return -1;
    return sa < sb ? -1 : 1;
}

// Minimal JSON field reader for GitHub's release JSON (string values only).
inline std::string jsonString(const std::string& js, const std::string& key, size_t from = 0, size_t* at = nullptr) {
    std::string pat = "\"" + key + "\"";
    size_t k = js.find(pat, from); if (k == std::string::npos) return "";
    size_t c = js.find(':', k + pat.size()); if (c == std::string::npos) return "";
    size_t q1 = js.find('"', c + 1); if (q1 == std::string::npos) return "";
    std::string v; size_t i = q1 + 1;
    for (; i < js.size() && js[i] != '"'; i++) { if (js[i] == '\\' && i + 1 < js.size()) { i++; } v += js[i]; }
    if (at) *at = i;
    return v;
}

struct Release { bool ok = false; std::string error, tag, zipUrl, pageUrl, notes; };

// Newest release, including pre-releases (alpha builds).
inline Release latest() {
    Release r;
    std::string js;
    int st = run("/usr/bin/curl -fsSL -m 10 -H 'Accept: application/vnd.github+json' -H 'User-Agent: Framely-updater' "
                 + q(std::string("https://api.github.com/repos/") + kRepo + "/releases?per_page=1") + " 2>/dev/null", &js);
    if (st != 0 || js.empty()) { r.error = "Couldn't reach GitHub. Check your internet connection."; return r; }
    r.tag = jsonString(js, "tag_name");
    if (r.tag.empty()) { r.error = "No releases published yet."; return r; }
    r.pageUrl = jsonString(js, "html_url");
    // find the asset whose browser_download_url ends with our zip name
    size_t pos = 0;
    while (true) {
        size_t at = 0; std::string u = jsonString(js, "browser_download_url", pos, &at);
        if (u.empty()) break;
        if (u.size() >= strlen(kAssetName) && u.compare(u.size() - strlen(kAssetName), strlen(kAssetName), kAssetName) == 0) { r.zipUrl = u; break; }
        pos = at + 1;
    }
    r.ok = true;
    return r;
}

// Path of the .ofx.bundle this code is running from.
inline std::string ownBundlePath() {
    Dl_info info{};
    if (!dladdr((const void*)&ownBundlePath, &info) || !info.dli_fname) return "";
    std::string p = info.dli_fname;                    // .../Framely.ofx.bundle/Contents/MacOS/Framely.ofx
    auto k = p.rfind("/Contents/MacOS/");
    return k == std::string::npos ? "" : p.substr(0, k);
}

// Download the release zip and replace the installed bundle's Contents. Resolve must restart to load it.
// Returns "" on success, else a message for the user.
inline std::string install(const Release& r, const std::string& bundle) {
    if (r.zipUrl.empty()) return "This release has no " + std::string(kAssetName) + " to install. Use Open Releases Page instead.";
    if (bundle.empty()) return "Couldn't find where Framely is installed.";
    char tmpl[] = "/tmp/framely-update-XXXXXX";
    if (!mkdtemp(tmpl)) return "Couldn't create a temporary folder.";
    std::string tmp = tmpl, zip = tmp + "/" + kAssetName, out = tmp + "/x";
    if (run("/usr/bin/curl -fsSL -m 120 -o " + q(zip) + " " + q(r.zipUrl) + " 2>/dev/null") != 0) return "Download failed.";
    if (run("/usr/bin/ditto -x -k " + q(zip) + " " + q(out) + " 2>/dev/null") != 0) return "Couldn't unpack the download.";
    std::string newBundle = out + "/Framely.ofx.bundle";
    struct stat sb{};
    if (stat((newBundle + "/Contents/MacOS/Framely.ofx").c_str(), &sb) != 0) return "The download doesn't contain a Framely plugin.";
    // Swap Contents by rename: the running copy stays valid in memory until Resolve restarts.
    std::string swap = "set -e; B=" + q(bundle) + "; N=" + q(newBundle) +
        "; rm -rf \"$B/Contents.old\"; mv \"$B/Contents\" \"$B/Contents.old\"; mv \"$N/Contents\" \"$B/Contents\"; rm -rf \"$B/Contents.old\"";
    int st;
    if (access(bundle.c_str(), W_OK) == 0 && access((bundle + "/Contents").c_str(), W_OK) == 0) {
        st = run("/bin/sh -c " + q(swap) + " 2>&1");
    } else {
        // Plugin folder belongs to the system: macOS asks for an administrator password.
        std::string as = "do shell script " + std::string("\"") + "/bin/sh -c " ;
        std::string esc; for (char c : q(swap)) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
        as += esc + "\" with prompt \"Framely needs your password to install the update.\" with administrator privileges";
        st = run("/usr/bin/osascript -e " + q(as) + " 2>&1");
    }
    run("rm -rf " + q(tmp));
    if (st != 0) return "Installing failed (the password prompt may have been cancelled).";
    return "";
}

inline void openURL(const std::string& url) { run("/usr/bin/open " + q(url)); }

} // namespace framely_update
