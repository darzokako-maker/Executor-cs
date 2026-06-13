import pymem
import pymem.process
import pyMeow
import time
import struct

# Offsets
dwEntityList = 0x24E76A0
dwViewMatrix = 0x2346B30
dwLocalPlayerPawn = 0x2341698

# Schemas
m_iTeamNum = 0x3E3
m_hPlayerPawn = 0x7DC
m_iHealth = 0x324
m_vOldOrigin = 0x127C

def read_matrix(pm, address):
    """View matrix'i float listesi olarak oku"""
    try:
        matrix_bytes = pm.read_bytes(address, 64)
        return list(struct.unpack('16f', matrix_bytes))
    except:
        return None

def world_to_screen_custom(matrix, pos, width=1920, height=1080):
    """Özel world to screen - pyMeow çalışmazsa yedek"""
    if not matrix or len(matrix) < 16:
        return None
    
    x, y, z = pos
    w = matrix[3] * x + matrix[7] * y + matrix[11] * z + matrix[15]
    
    if w < 0.01:
        return None
    
    ndc_x = (matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12]) / w
    ndc_y = (matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13]) / w
    
    screen_x = (width / 2) * (1 + ndc_x)
    screen_y = (height / 2) * (1 - ndc_y)
    
    return {"x": screen_x, "y": screen_y}

def world_to_screen(view_matrix, pos):
    """Önce pyMeow'u dene, çalışmazsa özel fonksiyonu kullan"""
    try:
        # pyMeow'un world_to_screen'ini dene
        result = pyMeow.world_to_screen(view_matrix, pos)
        if result and "x" in result and "y" in result:
            return result
    except:
        pass
    
    try:
        # Byte matrix'i float'a çevirip dene
        if isinstance(view_matrix, bytes):
            matrix = list(struct.unpack('16f', view_matrix))
            return world_to_screen_custom(matrix, pos)
    except:
        pass
    
    try:
        # Zaten float listesi ise direkt kullan
        if isinstance(view_matrix, list):
            return world_to_screen_custom(view_matrix, pos)
    except:
        pass
    
    return None

def get_entity(pm, entity_list, index, entity_size):
    """Entity'i al - entity_size otomatik ayarlanır"""
    try:
        list_entry = pm.read_longlong(entity_list + 0x8 * ((index & 0x7FFF) >> 9) + 0x10)
        if not list_entry:
            return None
        
        controller = pm.read_longlong(list_entry + entity_size * (index & 0x1FF))
        return controller
    except:
        return None

def main():
    try:
        pm = pymem.Pymem("cs2.exe")
        client = pymem.process.module_from_name(pm.process_handle, "client.dll").lpBaseOfDll
        print(f"[+] CS2'ye bağlanıldı - client.dll: 0x{client:X}")
    except:
        print("[-] CS2 bulunamadı!")
        return

    # Overlay başlat
    try:
        pyMeow.overlay_init("Counter-Strike 2", fps=60)
        print("[+] Overlay başlatıldı")
    except:
        print("[-] Overlay başlatılamadı!")
        return

    # Test edilecek entity size'ları (otomatik bulana kadar dene)
    entity_sizes = [0x78, 0x70, 0x80, 0x88, 0x68]
    active_size = None
    last_test_time = 0
    test_interval = 5  # Her 5 saniyede bir test et
    
    print("[*] Entity size tespit ediliyor...")
    print("[*] F1 ile çıkış yapabilirsiniz")
    
    while pyMeow.overlay_loop():
        if pyMeow.get_key_state(0x70):
            print("[+] Çıkış yapılıyor...")
            break
        
        pyMeow.begin_drawing()
        
        try:
            # Ana adresleri oku
            view_matrix = pm.read_bytes(client + dwViewMatrix, 64)
            entity_list = pm.read_longlong(client + dwEntityList)
            local_pawn = pm.read_longlong(client + dwLocalPlayerPawn)
            
            if not entity_list or not local_pawn or not view_matrix:
                pyMeow.end_drawing()
                time.sleep(0.001)
                continue
            
            local_team = pm.read_int(local_pawn + m_iTeamNum)
            
            # Entity size otomatik tespiti
            current_time = time.time()
            if active_size is None or (current_time - last_test_time) > test_interval:
                for test_size in entity_sizes:
                    try:
                        test_entity = get_entity(pm, entity_list, 1, test_size)
                        if test_entity:
                            test_team = pm.read_int(test_entity + m_iTeamNum)
                            if test_team in [2, 3]:  # T veya CT takımı
                                if active_size != test_size:
                                    print(f"[+] Entity size bulundu: 0x{test_size:X}")
                                active_size = test_size
                                break
                    except:
                        continue
                last_test_time = current_time
            
            # Entity size bulunamadıysa devam etme
            if active_size is None:
                pyMeow.end_drawing()
                time.sleep(0.001)
                continue
            
            # Oyuncuları tara
            for i in range(1, 64):
                try:
                    controller = get_entity(pm, entity_list, i, active_size)
                    if not controller:
                        continue
                    
                    controller_team = pm.read_int(controller + m_iTeamNum)
                    if controller_team == local_team or controller_team == 0:
                        continue
                    
                    pawn_handle = pm.read_uint(controller + m_hPlayerPawn)
                    if not pawn_handle or pawn_handle == 0xFFFFFFFF:
                        continue
                    
                    pawn_index = pawn_handle & 0x7FFF
                    pawn = get_entity(pm, entity_list, pawn_index, active_size)
                    
                    if not pawn or pawn == local_pawn:
                        continue
                    
                    health = pm.read_int(pawn + m_iHealth)
                    if health <= 0 or health > 100:
                        continue
                    
                    # Pozisyon oku
                    x = pm.read_float(pawn + m_vOldOrigin)
                    y = pm.read_float(pawn + m_vOldOrigin + 4)
                    z = pm.read_float(pawn + m_vOldOrigin + 8)
                    
                    # World to screen (otomatik format algılama)
                    foot_screen = world_to_screen(view_matrix, [x, y, z])
                    head_screen = world_to_screen(view_matrix, [x, y, z + 72.0])
                    
                    if not foot_screen or not head_screen:
                        continue
                    
                    # ESP çizimi
                    h = abs(foot_screen["y"] - head_screen["y"])
                    if h < 5:
                        continue
                    
                    w = h * 0.35
                    box_x = foot_screen["x"] - w / 2
                    box_y = head_screen["y"]
                    
                    # Takım rengi
                    color = pyMeow.rgb("red") if controller_team == 2 else pyMeow.rgb("blue")
                    
                    # Kutu
                    pyMeow.draw_rectangle_lines(box_x, box_y, w, h, color, 2.0)
                    
                    # Can barı arka plan
                    bar_w = 4
                    bar_x = box_x - bar_w - 2
                    pyMeow.draw_rectangle(bar_x, box_y, bar_w, h, pyMeow.rgb("gray"))
                    
                    # Can barı doluluk
                    health_h = h * (health / 100)
                    health_y = foot_screen["y"] - health_h
                    
                    if health > 70:
                        health_color = pyMeow.rgb("green")
                    elif health > 30:
                        health_color = pyMeow.rgb("yellow")
                    else:
                        health_color = pyMeow.rgb("red")
                    
                    pyMeow.draw_rectangle(bar_x, health_y, bar_w, health_h, health_color)
                    
                except:
                    continue
                    
        except Exception as e:
            pass
        
        pyMeow.end_drawing()
        time.sleep(0.001)
    
    pyMeow.overlay_close()
    print("[+] Program kapatıldı")

if __name__ == "__main__":
    main()
