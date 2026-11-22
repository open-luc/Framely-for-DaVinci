// Tests FramelyUpdate.h against the real GitHub release: version compare, latest(), install() into a temp bundle.
#include "FramelyUpdate.h"
#include <cassert>
#include <iostream>
using namespace framely_update;
int main(int argc, char** argv) {
    assert(compareVersions("v0.1.0", "0.0.9") > 0 && compareVersions("0.1.10", "0.1.9") > 0);
    assert(compareVersions("v0.1.0", "0.1.0") == 0 && compareVersions("0.2.0", "0.2.0-alpha") > 0);
    Release r = latest();
    std::cout << "ok=" << r.ok << " tag=" << r.tag << " zip=" << r.zipUrl << " err=" << r.error << "\n";
    if (!r.ok || argc < 2) return r.ok ? 0 : 1;
    std::string e = install(r, argv[1]);
    std::cout << "install: " << (e.empty() ? "OK" : e) << "\n";
    return e.empty() ? 0 : 1;
}
