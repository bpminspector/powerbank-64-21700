// PowerBank64 central control strip — parametric prototype, dimensions in mm.
// Open in OpenSCAD. Export the panel and retainer separately by changing `part`.
// Verify the switch, LED and USB module cutouts against the actual hardware first.

part = "panel"; // "panel" or "lcd_retainer"

panel_w = 50;
panel_l = 190;
panel_t = 3;
corner_r = 2.5;

lcd_window = 15.6;
lcd_board_w = 21.74;
lcd_board_l = 29.24;
lcd_pocket_depth = 1.6;
lcd_center_from_top = 26;
lcd_retainer_t = 1.6;
lcd_retainer_w = 38;
lcd_retainer_l = 42;
lcd_screw_d = 2.2; // M2 clearance; use screws + nuts or inserts in a thicker revision

toggle_hole_d = 6.5; // nominal M6 panel switches; measure actual bushing
led_hole_d = 3.2;    // nominal 3 mm LED bezel
usb_slot_w = 28;     // adjustable placeholder for one dual-port USB module
usb_slot_h = 8;
mount_hole_d = 3.2;  // M3 clearance
engrave_labels = true;

// SVG coordinates measured from its top edge; convert to centered OpenSCAD Y.
function cy_from_top(y) = panel_l / 2 - y;
lcd_cy = cy_from_top(lcd_center_from_top);

module rounded_plate(w, l, h, r) {
    linear_extrude(height=h)
        offset(r=r)
            square([w-2*r, l-2*r], center=true);
}

module bore_at(x, top_y, d, height=panel_t+0.4) {
    translate([x-panel_w/2, cy_from_top(top_y), -0.2])
        cylinder(d=d, h=height, $fn=48);
}

module panel_body() {
    difference() {
        rounded_plate(panel_w, panel_l, panel_t, corner_r);

        // LCD viewing window and rear pocket for the 21.74 x 29.24 mm PCB.
        translate([-lcd_window/2, lcd_cy-lcd_window/2, -0.2])
            cube([lcd_window, lcd_window, panel_t+0.4]);
        translate([-lcd_board_w/2, lcd_cy-lcd_board_l/2, -0.01])
            cube([lcd_board_w, lcd_board_l, lcd_pocket_depth+0.01]);

        // Retainer screw holes around the LCD board (M2 clearance).
        for (x = [-16.5, 16.5], y = [-18.5, 18.5])
            translate([x, lcd_cy+y, -0.2]) cylinder(d=lcd_screw_d, h=panel_t+0.4, $fn=32);

        // Four generic panel mounting holes.
        for (x = [-panel_w/2+5, panel_w/2-5], top_y = [5, panel_l-5])
            translate([x, cy_from_top(top_y), -0.2]) cylinder(d=mount_hole_d, h=panel_t+0.4, $fn=40);

        // Three shallow tactile zones (recess only, not through-holes).
        for (x = [-15, 0, 15])
            translate([x, cy_from_top(58), panel_t-0.45]) cylinder(d=8.4, h=0.5, $fn=64);

        // Four logic toggles, with one LED indicator beside each.
        for (top_y = [84, 103, 122, 141]) {
            translate([-8, cy_from_top(top_y), -0.2]) cylinder(d=toggle_hole_d, h=panel_t+0.4, $fn=48);
            translate([14, cy_from_top(top_y), -0.2]) cylinder(d=led_hole_d, h=panel_t+0.4, $fn=40);
        }

        // Placeholder apertures for two dual-port USB modules.
        for (top_y = [156, 172])
            translate([-usb_slot_w/2, cy_from_top(top_y)-usb_slot_h/2, -0.2])
                minkowski() {
                    cube([usb_slot_w-1.2, usb_slot_h-1.2, panel_t+0.4]);
                    cylinder(r=0.6, h=0.01, $fn=16);
                }

        // Optional microphone grille. Omit/fill these holes if the mic is elsewhere.
        /*
        for (x = [-7, -3.5, 0, 3.5, 7])
            translate([x, cy_from_top(184), -0.2]) cylinder(d=1.3, h=panel_t+0.4, $fn=24);

        if (engrave_labels) {
            for (i = [0:3]) {
                labels = ["MAIN", "12V", "24V", "USB"];
                translate([-22, cy_from_top([84,103,122,141][i])-0.9, panel_t-0.45])
                    linear_extrude(height=0.5) text(labels[i], size=2.3, font="Liberation Sans:style=Bold");
            }
            for (i = [0:2]) {
                labels = ["<", "OK", ">"];
                translate([-15 + i*15 - (i==1 ? 2 : 1), cy_from_top(58)-1, panel_t-0.45])
                    linear_extrude(height=0.5) text(labels[i], size=2.2, font="Liberation Sans:style=Bold");
            }
        }
        */
    }
}

module lcd_retainer() {
    difference() {
        translate([0, lcd_cy, 0])
            linear_extrude(height=lcd_retainer_t)
                difference() {
                    square([lcd_retainer_w, lcd_retainer_l], center=true);
                    square([lcd_window+0.6, lcd_window+0.6], center=true);
                }
        for (x = [-16.5, 16.5], y = [-18.5, 18.5])
            translate([x, lcd_cy+y, -0.2]) cylinder(d=lcd_screw_d, h=lcd_retainer_t+0.4, $fn=32);
    }
}

if (part == "panel") panel_body();
else if (part == "lcd_retainer") lcd_retainer();
else echo("Set part to panel or lcd_retainer");
