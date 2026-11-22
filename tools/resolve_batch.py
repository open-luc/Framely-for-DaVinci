# Paste into the Resolve MCP run_script with CFG set: builds 3 timelines (4K/HD/Vertical) with the 3 test images,
# applies a fresh Framely (which starts from the saved-defaults file), and renders one PNG frame per clip.
import time
mp = project.GetMediaPool()
clips = {c.GetName(): c for c in mp.GetRootFolder().GetClipList()}
FX = "Open FX/Framely/Framely – Picture in Picture"
OUT = "/Users/luc/Developer/Framely/build/resolve"
names = ["Framely_portrait.png", "Framely_square.png", "Framely_landscape.png"]
jobs = []
project.SetCurrentRenderFormatAndCodec("png", "RGB8")
for res, w, h in [("4k", 3840, 2160), ("hd", 1920, 1080), ("vert", 2160, 3840)]:
    tn = f"T {CFG} {res}"
    tl = None
    for i in range(1, project.GetTimelineCount() + 1):
        if project.GetTimelineByIndex(i).GetName() == tn: tl = project.GetTimelineByIndex(i)
    if tl is None:
        tl = mp.CreateEmptyTimeline(tn); project.SetCurrentTimeline(tl)
        tl.SetSetting("useCustomSettings", "1"); tl.SetSetting("timelineResolutionWidth", str(w)); tl.SetSetting("timelineResolutionHeight", str(h))
        items = mp.AppendToTimeline([{"mediaPoolItem": clips[n]} for n in names])
        for it in items: it.AddEffect("ofx", FX)
    project.SetCurrentTimeline(tl)
    s0 = tl.GetStartFrame()
    for i, it in enumerate(tl.GetItemListInTrack("video", 1)):
        f = it.GetStart() + 100
        project.SetRenderSettings({"SelectAllFrames": False, "MarkIn": f, "MarkOut": f, "TargetDir": OUT,
                                   "CustomName": f"{CFG}_{res}_{names[i][8:-4]}_"})
        jobs.append(project.AddRenderJob())
project.StartRendering(jobs, isInteractiveMode=False)
t0 = time.time()
while project.IsRenderingInProgress() and time.time() - t0 < 50: time.sleep(0.5)
st = [project.GetRenderJobStatus(j).get("JobStatus") for j in jobs]
for j in jobs: project.DeleteRenderJob(j)
result = {"cfg": CFG, "status": st, "secs": round(time.time() - t0, 1)}
