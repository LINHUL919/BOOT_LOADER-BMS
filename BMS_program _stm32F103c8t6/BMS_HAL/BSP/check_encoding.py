import os

base = r'c:\Users\linhu\Desktop\BMS_program\BMS_HAL'
files_to_check = [
    r'Core\Src\main.c',
    r'Core\Src\usart.c',
    r'Core\Inc\usart.h',
    r'BSP\bms_soc.c',
    r'BSP\bms_soc.h',
    r'BSP\bq76940.h',
    r'Core\Src\stm32f1xx_it.c',
    r'Core\Src\gpio.c',
    r'Core\Src\dma.c',
    r'Core\Inc\FreeRTOSConfig.h',
]

garbled_chars = '\u9422\u57ab\u771c\u7ba0\u946a\u608a'
good_cn_chars = '\u7535\u6c60\u7ba1\u7406\u521d\u59cb\u5316\u4efb\u52a1'

for fn in files_to_check:
    fp = os.path.join(base, fn)
    if not os.path.exists(fp):
        print(f'{fn}: NOT FOUND')
        continue
    with open(fp, 'r', encoding='utf-8-sig', errors='replace') as f:
        text = f.read()
    has_garbled = any(c in text for c in garbled_chars)
    has_good_cn = any(c in text for c in good_cn_chars)
    print(f'{fn}: garbled={has_garbled}, good_cn={has_good_cn}, size={len(text)}')
