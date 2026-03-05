/*
 * Vibe Bike Display Mount v4.1
 * ============================
 * Coords: X=width, Y=height(bottom->top), Z=depth(front->back)
 * Board slides in from front (Z=0). Screen faces out.
 * 4 L-bracket spacers behind board create battery space.
 * Plate slot and USB on bottom (Y=0).
 *
 * Front view (looking along Z into tray):
 *   ┌─────────────────────┐
 *   │ ┌──┐           ┌──┐ │  <- brackets (top corners)
 *   │ └──┤           ├──┘ │
 *   │    │  board    │    │
 *   │    │  slides   │    │
 *   │    │  in here  │    │
 *   │ ┌──┤           ├──┐ │
 *   │ └──┘           └──┘ │  <- brackets (bottom corners)
 *   └─────────────────────┘
 *
 * Print: Open side (Z=0) facing up.
 */

$fn = 60;

// ================================================================
//  MEASUREMENTS
// ================================================================

BOARD_W      = 50.0;    // PCB width (X)
BOARD_H      = 86.0;    // PCB height (Y)
BOARD_T      = 10.6;    // total thickness (Z, depth when slid in)

PLATE_W      = 20.0;    // metal plate width
PLATE_T      = 2.5;     // metal plate thickness

// ================================================================
//  DESIGN PARAMETERS
// ================================================================

WALL         = 2.5;     // wall thickness
TOL          = 0.2;     // tolerance (gap between board back and bracket face)
BATT_SPACE   = 12.0;    // space behind board for battery/wires (Z)
BACK_WALL    = 6.0;     // back wall thickness (holds plate slot)
SLOT_H       = 25.0;    // plate slot engagement height (Y)
USB_W        = 14.0;    // USB-C slot width
CORNER_R     = 4.0;     // outer corner radius

// Corner brackets (in X-Y plane, extend along Z)
BOARD_GAP    = 0.25;    // clearance between board edge and cavity wall (friction fit)
CONN_EXTRA   = 2.0;     // extra right-side clearance for IO35 connector + wires
BRACKET_W    = 3.0;     // bracket arm width (> BOARD_GAP so it overlaps under board)
BRACKET_ARM  = 10.0;    // bracket arm length along wall

// ================================================================
//  COMPUTED
// ================================================================

inner_w    = BOARD_W + BOARD_GAP * 2 + CONN_EXTRA; // 52.5 (extra on right for connector)
inner_h    = BOARD_H + BOARD_GAP * 2;           // 86.5
cavity_d   = BOARD_T + TOL + BATT_SPACE;        // 22.8

frame_w    = inner_w + WALL * 2;                // 57.5
frame_h    = inner_h + WALL * 2;                // 91.5
frame_d    = cavity_d + BACK_WALL;              // 28.8

// Board position (shifted right in cavity, extra gap on left/low-X = screen-right for connector)
board_x    = WALL + BOARD_GAP + CONN_EXTRA;     // 4.75
board_y    = WALL + BOARD_GAP;                  // 2.75

// Plate slot (centered X, in back wall)
slot_w     = PLATE_W + TOL * 2;
slot_t     = PLATE_T + TOL * 2;
slot_z     = frame_d - BACK_WALL + (BACK_WALL - slot_t) / 2;

// ================================================================
//  HELPERS
// ================================================================

module rounded_box(w, h, d, r) {
    translate([r, r, 0])
        minkowski() {
            cube([w - r*2, h - r*2, d]);
            cylinder(r=r, h=0.001);
        }
}

// ================================================================
//  MODEL
// ================================================================

module mount() {
    difference() {
        // ---- Build body + brackets, then cut USB last ----
        union() {
            difference() {
                // Solid body (rounded corners in X-Y)
                rounded_box(frame_w, frame_h, frame_d, CORNER_R);

                // Cavity (open at front Z=0, board slides in here)
                translate([WALL, WALL, -1])
                    cube([inner_w, inner_h, cavity_d + 1]);

                // Plate slot (in back wall, open at bottom Y=0)
                translate([(frame_w - slot_w) / 2, -1, slot_z])
                    cube([slot_w, SLOT_H + WALL + 1, slot_t]);
            }

            // 4 L-bracket spacers (behind board, in battery zone)
            bz = BOARD_T + TOL;   // bracket start Z (just behind board)
            bd = BATT_SPACE;       // bracket depth along Z

            // Bottom-left
            translate([WALL, WALL, bz]) {
                cube([BRACKET_ARM, BRACKET_W, bd]);
                cube([BRACKET_W, BRACKET_ARM, bd]);
            }

            // Bottom-right
            translate([WALL + inner_w, WALL, bz])
                mirror([1, 0, 0]) {
                    cube([BRACKET_ARM, BRACKET_W, bd]);
                    cube([BRACKET_W, BRACKET_ARM, bd]);
                }

            // Top-left
            translate([WALL, WALL + inner_h, bz])
                mirror([0, 1, 0]) {
                    cube([BRACKET_ARM, BRACKET_W, bd]);
                    cube([BRACKET_W, BRACKET_ARM, bd]);
                }

            // Top-right
            translate([WALL + inner_w, WALL + inner_h, bz])
                mirror([1, 0, 0]) mirror([0, 1, 0]) {
                    cube([BRACKET_ARM, BRACKET_W, bd]);
                    cube([BRACKET_W, BRACKET_ARM, bd]);
                }
        }

        // USB-C slot (cut LAST so it goes through wall + brackets)
        // Centered on board (not frame, since cavity is asymmetric)
        translate([board_x + (BOARD_W - USB_W) / 2, -0.01, 0])
            cube([USB_W, board_y + 5, cavity_d]);
    }
}

// ================================================================
//  RENDER
// ================================================================

mount();

// ================================================================
//  VISUALIZATION  (uncomment to check fit)
// ================================================================

// Board (slid in from front):
// color("green", 0.25)
//     translate([board_x, board_y, 0])
//         cube([BOARD_W, BOARD_H, BOARD_T]);

// Metal plate:
// color("silver", 0.3)
//     translate([(frame_w - PLATE_W)/2, -20, slot_z + TOL])
//         cube([PLATE_W, SLOT_H + 25, PLATE_T]);
