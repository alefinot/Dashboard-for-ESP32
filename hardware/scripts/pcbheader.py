"""Minimal valid KiCad 9/10 4-layer board header (layer table + stackup + plot params)."""
LAYERS = """	(layers
		(0 "F.Cu" signal)
		(4 "In1.Cu" signal)
		(6 "In2.Cu" signal)
		(2 "B.Cu" signal)
		(9 "F.Adhes" user "F.Adhesive")
		(11 "B.Adhes" user "B.Adhesive")
		(13 "F.Paste" user)
		(15 "B.Paste" user)
		(5 "F.SilkS" user "F.Silkscreen")
		(7 "B.SilkS" user "B.Silkscreen")
		(1 "F.Mask" user)
		(3 "B.Mask" user)
		(17 "Dwgs.User" user "User.Drawings")
		(19 "Cmts.User" user "User.Comments")
		(21 "Eco1.User" user "User.Eco1")
		(23 "Eco2.User" user "User.Eco2")
		(25 "Edge.Cuts" user)
		(27 "Margin" user)
		(31 "F.CrtYd" user "F.Courtyard")
		(29 "B.CrtYd" user "B.Courtyard")
		(35 "F.Fab" user)
		(33 "B.Fab" user)
	)"""

STACKUP = """		(stackup
			(layer "F.SilkS" (type "Top Silk Screen"))
			(layer "F.Paste" (type "Top Solder Paste"))
			(layer "F.Mask" (type "Top Solder Mask") (thickness 0.01))
			(layer "F.Cu" (type "copper") (thickness 0.035))
			(layer "dielectric 1" (type "prepreg") (thickness 0.21) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
			(layer "In1.Cu" (type "copper") (thickness 0.035))
			(layer "dielectric 2" (type "core") (thickness 0.91) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
			(layer "In2.Cu" (type "copper") (thickness 0.035))
			(layer "dielectric 3" (type "prepreg") (thickness 0.21) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
			(layer "B.Cu" (type "copper") (thickness 0.035))
			(layer "B.Mask" (type "Bottom Solder Mask") (thickness 0.01))
			(layer "B.Paste" (type "Bottom Solder Paste"))
			(layer "B.SilkS" (type "Bottom Silk Screen"))
			(copper_finish "None")
			(dielectric_constraints no)
		)"""

PLOT = """		(pcbplotparams
			(layerselection 0x0000000_0100000_00000000000000000000000000000000000000000000000000000000000f003f)
			(plot_on_all_layers_selection 0x0000000_0000000_00000000000000000000000000000000000000000000000000000000)
			(disableapertmacros no)
			(usegerberextensions no)
			(usegerberattributes yes)
			(usegerberadvancedattributes yes)
			(creategerberjobfile yes)
			(svgprecision 4)
			(mode 1)
			(useauxorigin no)
			(dxfimperialunits yes)
			(psnegative no)
			(plotframeref no)
			(outputformat 1)
			(drillshape 0)
			(scaleselection 1)
			(outputdirectory "plot/")
		)"""


def header(nets, title="Dashboard++ Carrier", rev="0.1", thickness=1.6):
    nl = "\n".join('	(net %d "%s")' % (i, n) for i, n in enumerate(nets))
    return ('(kicad_pcb\n	(version 20241229)\n	(generator "pcbnew")\n	(generator_version "9.0")\n'
            '	(general\n		(thickness %.4f)\n		(legacy_teardrops no)\n	)\n'
            '	(paper "A4")\n	(title_block\n		(title "%s")\n		(rev "%s")\n		(company "Dashboard++ for ESP32")\n	)\n'
            '%s\n	(setup\n%s\n		(pad_to_mask_clearance 0)\n		(allow_soldermask_bridges_in_footprints no)\n'
            '		(tenting front back)\n%s\n	)\n\n	%s\n'
            % (thickness, title, rev, LAYERS, STACKUP, PLOT, nl))
