import re

proj_path = 'd:/电机/DAPlink/GD32-HID/GD32-HID/DAPlink/MDK-ARM/DAPlink.uvprojx'

with open(proj_path, 'r', encoding='utf-8') as f:
    content = f.read()

old_define = 'USE_STDPERIPH_DRIVER,GD32F10X_MD,USBD_BOS_ENABLE'
new_define = 'USE_STDPERIPH_DRIVER,GD32F10X_MD'

count = content.count(old_define)
print(f'Found {count} occurrences')
content = content.replace(old_define, new_define)

with open(proj_path, 'w', encoding='utf-8') as f:
    f.write(content)

print('Removed USBD_BOS_ENABLE')
