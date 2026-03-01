/*
 * Vibe Bike Display Mount - Front Cap v1.0
 * =========================================
 * Slides over front of mount to cover PCB edges,
 * with a cutout showing only the screen.
 *
 * Coords: X=width, Y=height, Z=depth
 *   Z=0 = visible front face (print this on bed)
 *   Z+  = lip extends backward toward mount
 *
 * Front view:
 *   ┌───────────────────────────┐
 *   │                           │
 *   │   ┌───────────────────┐   │
 *   │   │                   │   │
 *   │   │   screen cutout   │   │
 *   │   │                   │   │
 *   │   └───────────────────┘   │
 *   │                           │
 *   └───────────────────────────┘
 *
 * Print: Front face (Z=0) on bed, lip pointing up.
 */

$fn = 60;

// ================================================================
//  MOUNT DIMENSIONS (must match bike_mount.scad)
// ================================================================

BOARD_W      = 50.0;
BOARD_H      = 86.0;
WALL         = 2.5;
BOARD_GAP    = 0.25;
CORNER_R     = 4.0;
USB_W        = 14.0;

inner_w      = BOARD_W + BOARD_GAP * 2;
inner_h      = BOARD_H + BOARD_GAP * 2;
frame_w      = inner_w + WALL * 2;           // 55.5
frame_h      = inner_h + WALL * 2;           // 91.5

board_x      = WALL + BOARD_GAP;             // 2.75
board_y      = WALL + BOARD_GAP;             // 2.75

// ================================================================
//  SCREEN DIMENSIONS (ES3C28P datasheet)
// ================================================================

SCREEN_VA_W  = 43.60;    // visible area width
SCREEN_VA_H  = 58.05;    // visible area height

// Screen centered on board
screen_x     = board_x + (BOARD_W - SCREEN_VA_W) / 2;   // 5.95
screen_y     = board_y + (BOARD_H - SCREEN_VA_H) / 2;   // 16.725

// ================================================================
//  CAP DESIGN PARAMETERS
// ================================================================

CAP_T        = 1.5;      // plate thickness
LIP_T        = 1.5;      // lip wall thickness
LIP_D        = 4.0;      // lip depth (wraps back over mount)
FIT_TOL      = 0.3;      // clearance for cap to slide onto mount
SCREEN_M     = 0.5;      // margin around screen cutout
SCREEN_R     = 1.5;      // screen cutout corner radius

// ================================================================
//  COMPUTED
// ================================================================

// Offset from cap origin to mount frame origin
offset       = LIP_T + FIT_TOL;

// Cap outer dimensions
cap_w        = frame_w + offset * 2;         // 59.1
cap_h        = frame_h + offset * 2;         // 95.1
cap_r        = CORNER_R + offset;            // 5.8

// Lip inner pocket (matches mount frame + tolerance)
lip_inner_w  = frame_w + FIT_TOL * 2;
lip_inner_h  = frame_h + FIT_TOL * 2;
lip_inner_r  = CORNER_R + FIT_TOL;

// Screen cutout (in cap coordinates)
cut_w        = SCREEN_VA_W + SCREEN_M * 2;   // 44.6
cut_h        = SCREEN_VA_H + SCREEN_M * 2;   // 59.05
cut_x        = offset + screen_x - SCREEN_M; // 7.25
cut_y        = offset + screen_y - SCREEN_M; // 18.025

// USB cable clearance in bottom lip
usb_cut_w    = USB_W + 2;
usb_cut_x    = (cap_w - usb_cut_w) / 2;

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

module rounded_rect(w, h, r) {
    translate([r, r])
        minkowski() {
            square([w - r*2, h - r*2]);
            circle(r=r);
        }
}

// ================================================================
//  MODEL
// ================================================================

module cap() {
    difference() {
        union() {
            // Front plate (Z=0 to CAP_T)
            rounded_box(cap_w, cap_h, CAP_T, cap_r);

            // Lip (Z=CAP_T to CAP_T+LIP_D, wraps around mount frame)
            translate([0, 0, CAP_T])
                difference() {
                    rounded_box(cap_w, cap_h, LIP_D, cap_r);
                    translate([LIP_T, LIP_T, -1])
                        rounded_box(lip_inner_w, lip_inner_h, LIP_D + 2, lip_inner_r);
                }
        }

        // Screen cutout (through plate)
        translate([cut_x, cut_y, -1])
            linear_extrude(CAP_T + 2)
                rounded_rect(cut_w, cut_h, SCREEN_R);

        // USB cable clearance (notch in bottom lip)
        translate([usb_cut_x, -0.01, CAP_T - 0.01])
            cube([usb_cut_w, offset + 0.02, LIP_D + 0.02]);
    }
}

// ================================================================
//  RENDER
// ================================================================

cap();
