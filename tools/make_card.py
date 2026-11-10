"""Printable 4x6 in setup card (card/setup_card.html -> card/setup_card.pdf via headless Chrome)."""
import os, subprocess, io, base64
import qrcode, qrcode.image.svg

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
GUIDE = 'https://ds3design.github.io/nextmile-tracker/'

qr = qrcode.make(GUIDE, image_factory=qrcode.image.svg.SvgPathImage, box_size=10, border=1)
buf = io.BytesIO(); qr.save(buf)
qr_svg = buf.getvalue().decode().split('?>', 1)[-1]
logo = base64.b64encode(open(os.path.join(ROOT, 'assets', 'nextmile_logo.png'), 'rb').read()).decode()

html = f'''<!doctype html><html><head><meta charset=utf-8><title>Next Mile Tracker setup card</title><style>
@page {{ size: 4in 6in; margin: 0 }}
body {{ margin:0; font-family: Oswald, 'Arial Narrow', Arial, sans-serif; color:#000 }}
.card {{ width:4in; height:6in; box-sizing:border-box; padding:.28in .3in; display:flex; flex-direction:column }}
.top {{ display:flex; align-items:center; gap:.12in; border-bottom:3px solid #e00; padding-bottom:.08in }}
.top img {{ width:.95in }}
h1 {{ font-size:22pt; margin:0; line-height:1; letter-spacing:.02em; text-transform:uppercase }}
h1 small {{ display:block; font-size:10pt; color:#e00; letter-spacing:.1em; margin-top:3px }}
ol {{ font-family: Arial, sans-serif; font-size:10.2pt; line-height:1.35; padding-left:.22in; margin:.14in 0 0 }}
li {{ margin-bottom:.07in }} b {{ color:#000 }} .r {{ color:#e00; font-weight:700 }}
.foot {{ margin-top:auto; display:flex; gap:.15in; align-items:center; font-family:Arial,sans-serif; font-size:8.6pt; color:#333 }}
.foot svg {{ width:.95in; height:.95in; flex:none }}
</style></head><body><div class=card>
<div class=top><img src="data:image/png;base64,{logo}"><h1>Next Mile Tracker<small>Your miles, all year</small></h1></div>
<ol>
<li>Plug the tracker into any USB phone charger.</li>
<li>When the screen says <b>Let's get connected</b>, scan its QR code with your phone camera (or open WiFi settings and join <span class=r>NextMile-Tracker</span>).</li>
<li>A setup page opens. Tap <b>Configure WiFi</b>, pick your home WiFi and enter its password. <i>2.4&nbsp;GHz WiFi only.</i></li>
<li>Scan the new QR code on the screen and pick your watch: Garmin, Apple Watch, Fitbit / Pixel or Polar.</li>
<li>Done. Your miles for the year update every 30 minutes and start fresh every January 1.</li>
</ol>
<p style="font-family:Arial;font-size:9pt;margin:.04in 0 0"><b>Tap</b> the screen for goal &amp; settings. <b>Hold 10 seconds</b> to erase and start over.</p>
<div class=foot>{qr_svg}<div>Help, goal settings and troubleshooting:<br><b>ds3design.github.io/nextmile-tracker</b><br><br>Made by DS3design for Next Mile Apparel &middot; Pacegiving 2026</div></div>
</div></body></html>'''

src = os.path.join(ROOT, 'card', 'setup_card.html')
open(src, 'w').write(html)
pdf = os.path.join(ROOT, 'card', 'setup_card.pdf')
subprocess.run(['google-chrome', '--headless', '--disable-gpu', '--no-sandbox', '--no-pdf-header-footer',
                f'--print-to-pdf={pdf}', 'file://' + os.path.abspath(src)], check=True, capture_output=True)
print('wrote', pdf)
