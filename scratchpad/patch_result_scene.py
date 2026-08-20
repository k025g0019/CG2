import io

SRC = r"C:\kogakuin\LE1\CG2\Assets\Scenes\WaterRailShooter_0817.scene"
DST = r"C:\kogakuin\LE1\CG2\Assets\Scenes\Result.scene"

def read_lines(path):
    with io.open(path, "r", encoding="utf-8", newline="") as f:
        return f.read().split("\n")

src_lines = read_lines(SRC)
dst_lines = read_lines(DST)

# Trim trailing empty line(s) tracking
dst_trailing_newline = dst_lines[-1] == ""
if dst_trailing_newline:
    dst_lines = dst_lines[:-1]

def find_line(lines, prefix_fields):
    """prefix_fields: list like ['GameObject','107'] or ['Component','107','0']"""
    target = "|".join(prefix_fields)
    for line in lines:
        parts = line.split("|")
        if len(parts) >= len(prefix_fields) and parts[:len(prefix_fields)] == prefix_fields:
            return line
    raise ValueError("not found: " + target)

def clone_with_new_owner(line, new_owner_id, field_index=1):
    parts = line.split("|")
    parts[field_index] = str(new_owner_id)
    return "|".join(parts)

# --- Source template lines ---
go_88 = find_line(src_lines, ["GameObject", "88"])
comp_88_0 = find_line(src_lines, ["Component", "88", "0"])
comp_88_26 = find_line(src_lines, ["Component", "88", "26"])
comp_88_81 = find_line(src_lines, ["Component", "88", "81"])
comp_88_82 = find_line(src_lines, ["Component", "88", "82"])

go_107 = find_line(src_lines, ["GameObject", "107"])
comp_107_0 = find_line(src_lines, ["Component", "107", "0"])
comp_107_84 = find_line(src_lines, ["Component", "107", "84"])

go_109 = find_line(src_lines, ["GameObject", "109"])
comp_109_0 = find_line(src_lines, ["Component", "109", "0"])
comp_109_84 = find_line(src_lines, ["Component", "109", "84"])

# --- New ids ---
NEW_CANVAS = 6
NEW_CLEAR_TEXT = 7
NEW_FAILED_TEXT = 8

# --- Build new Canvas GameObject line (top-level, active) ---
new_go_canvas = "GameObject|{}|-1|Canvas|0|0|0|0|0|0|1|1|1|1".format(NEW_CANVAS)
new_comp_canvas_0 = clone_with_new_owner(comp_88_0, NEW_CANVAS)
new_comp_canvas_26 = clone_with_new_owner(comp_88_26, NEW_CANVAS)
new_comp_canvas_81 = clone_with_new_owner(comp_88_81, NEW_CANVAS)
new_comp_canvas_82 = clone_with_new_owner(comp_88_82, NEW_CANVAS)

# --- MISSION CLEAR Text (green), child of new Canvas, inactive by default ---
new_go_clear = "GameObject|{}|{}|MISSION CLEAR Text|0|0|0|0|0|0|1|1|1|0".format(NEW_CLEAR_TEXT, NEW_CANVAS)
new_comp_clear_0 = clone_with_new_owner(comp_107_0, NEW_CLEAR_TEXT)
new_comp_clear_84 = clone_with_new_owner(comp_107_84, NEW_CLEAR_TEXT)

# --- MISSION FAILED Text (red, text "SHIP LOST" to match existing gameplay copy), child of new Canvas, inactive by default ---
new_go_failed = "GameObject|{}|{}|MISSION FAILED Text|0|0|0|0|0|0|1|1|1|0".format(NEW_FAILED_TEXT, NEW_CANVAS)
new_comp_failed_0 = clone_with_new_owner(comp_109_0, NEW_FAILED_TEXT)
new_comp_failed_84 = clone_with_new_owner(comp_109_84, NEW_FAILED_TEXT)

new_lines = [
    new_go_canvas,
    new_comp_canvas_0,
    new_comp_canvas_26,
    new_comp_canvas_81,
    new_comp_canvas_82,
    new_go_clear,
    new_comp_clear_0,
    new_comp_clear_84,
    new_go_failed,
    new_comp_failed_0,
    new_comp_failed_84,
]

# --- Structural sanity: field count must match template rows ---
def field_count(line):
    return len(line.split("|"))

checks = [
    (new_comp_canvas_0, comp_88_0),
    (new_comp_canvas_26, comp_88_26),
    (new_comp_canvas_81, comp_88_81),
    (new_comp_canvas_82, comp_88_82),
    (new_comp_clear_0, comp_107_0),
    (new_comp_clear_84, comp_107_84),
    (new_comp_failed_0, comp_109_0),
    (new_comp_failed_84, comp_109_84),
]
for new_l, template_l in checks:
    assert field_count(new_l) == field_count(template_l), "field count mismatch"

assert field_count(new_go_canvas) == field_count(go_88), "GameObject field count mismatch (canvas)"
assert field_count(new_go_clear) == field_count(go_107), "GameObject field count mismatch (clear)"
assert field_count(new_go_failed) == field_count(go_109), "GameObject field count mismatch (failed)"

out_lines = dst_lines + new_lines
if dst_trailing_newline:
    out_lines = out_lines + [""]

with io.open(DST, "w", encoding="utf-8", newline="") as f:
    f.write("\n".join(out_lines))

print("Wrote", len(new_lines), "new lines to", DST)
print("New GameObject ids:", NEW_CANVAS, NEW_CLEAR_TEXT, NEW_FAILED_TEXT)
