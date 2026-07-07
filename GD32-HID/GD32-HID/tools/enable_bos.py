proj_path = 'd:/电机/DAPlink/GD32-HID/GD32-HID/DAPlink/MDK-ARM/DAPlink.uvprojx'

with open(proj_path, 'r', encoding='utf-8') as f:
    content = f.read()

old = 'USE_STDPERIPH_DRIVER,GD32F10X_MD'
new = 'USE_STDPERIPH_DRIVER,GD32F10X_MD,USBD_BOS_ENABLE'

content = content.replace(old, new)

with open(proj_path, 'w', encoding='utf-8') as f:
    f.write(content)

print('USBD_BOS_ENABLE added')
