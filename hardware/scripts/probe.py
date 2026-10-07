import subprocess, kicadfmt as kf, pcbheader, os, sys
K=r"C:/Users/Kurose AE/AppData/Local/Programs/KiCad/10.0/bin/kicad-cli"
fp=kf.board_footprint("Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",(30,30),0,"J1","T",{"1":(1,"GND"),"2":(2,"SIG")})
pcb = pcbheader.header(["","GND","SIG"]) + fp + '\n'
pcb += '(gr_rect (start 20 20) (end 50 40) (stroke (width 0.1) (type solid)) (fill none) (layer "Edge.Cuts") (uuid "%s"))\n'%kf.uid()
pcb += '(gr_text "DASH" (at 40 30) (layer "F.SilkS") (uuid "%s") (effects (font (size 1.5 1.5))))\n'%kf.uid()
pcb += '(segment (start 30 30) (end 32.54 32.54) (width 0.4) (layer "F.Cu") (net 1) (uuid "%s"))\n'%kf.uid()
pcb += '(via (at 33 33) (size 0.8) (drill 0.4) (layers "F.Cu" "B.Cu") (net 1) (uuid "%s"))\n'%kf.uid()
pcb += ('(zone (net 1) (net_name "GND") (layer "In1.Cu") (uuid "%s") (hatch edge 0.5) (connect_pads (clearance 0.5))'
        ' (min_thickness 0.2) (fill yes (thermal_gap 0.5) (thermal_bridge_width 0.5))'
        ' (polygon (pts (xy 21 21) (xy 49 21) (xy 49 39) (xy 21 39))))\n')%kf.uid()
pcb += ')\n'
open('t.kicad_pcb','w',encoding='utf-8').write(pcb)
r=subprocess.run([K,"pcb","drc","--severity-all","-o","drc.txt","t.kicad_pcb"],capture_output=True,text=True)
print('drc exit',r.returncode,(r.stderr or '').strip()[:300])
if os.path.exists('drc.txt'):
    print(open('drc.txt',encoding='utf-8',errors='replace').read()[:1500])
    r2=subprocess.run([K,"pcb","export","gerbers","--check-zones","-o","plot","t.kicad_pcb"],capture_output=True,text=True)
    print('gerbers',r2.returncode,(r2.stderr or '').strip()[:300])
    print(sorted(os.listdir('plot'))[:16])
