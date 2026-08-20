import io

DST = r"C:\kogakuin\LE1\CG2\Assets\Scenes\Result.scene"

with io.open(DST, "r", encoding="utf-8", newline="") as f:
    lines = [l for l in f.read().split("\n") if l != ""]

game_objects = {}
for line in lines:
    parts = line.split("|")
    if parts[0] == "GameObject":
        gid = int(parts[1])
        parent = int(parts[2])
        name = parts[3]
        active = parts[-1]
        if gid in game_objects:
            print("DUPLICATE GameObject id:", gid)
        game_objects[gid] = (parent, name, active)

print("Total GameObjects:", len(game_objects))
for gid, (parent, name, active) in sorted(game_objects.items()):
    parent_ok = (parent == -1) or (parent in game_objects)
    print(f"id={gid} parent={parent} name={name!r} isActive={active} parent_valid={parent_ok}")

# Component owner reference check
comp_owner_ok = True
for line in lines:
    parts = line.split("|")
    if parts[0] in ("Component",):
        owner = int(parts[1])
        if owner not in game_objects:
            comp_owner_ok = False
            print("Component with missing owner:", owner)
print("All component owners valid:", comp_owner_ok)

# No stray/truncated lines: every line must start with a known row-type token
row_types = set(l.split("|")[0] for l in lines)
print("Row types present:", row_types)
