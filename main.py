import pymem
import pymem.process
import pyMeow
import time

# --- YÜKLEDİĞİN OFFSETS.HPP VERİLERİ ---
offsets = {
    "dwEntityList": 0x24E76A0,       
    "dwViewMatrix": 0x2346B30,       
    "dwLocalPlayerPawn": 0x2341698   
}

# --- YÜKLEDİĞİN SCHEMAS DÖKÜMÜ VE EN GÜNCEL VERİ HİZALAMALARI ---
schemas = {
    "m_sSanitizedPlayerName": 0x720, # client.dll -> Controller Name dizilim alanı
    "m_iTeamNum": 0x3E3,             # client.dll -> C_BaseEntity takım kimliği
    "m_hPlayerPawn": 0x7DC,          # client.dll -> Controller Pawn bağlantı kolu
    "m_iHealth": 0x324,              # client.dll -> C_BaseEntity can bütünlüğü
    "m_vOldOrigin": 0x127C           # client.dll -> C_BasePlayerPawn 3D koordinatı
}

def main():
    try:
        # CS2 sürecine ve ana motor kütüphanesine bağlanma
        pm = pymem.Pymem("cs2.exe")
        client = pymem.process.module_from_name(pm.process_handle, "client.dll").lpBaseOfDll
        print("CS2 Bellek Senkronizasyonu Basarili!")
    except Exception:
        print("CS2 bulunamadi! Lutfen oyunu acin ve programi Yonetici olarak calistirin.")
        return

    # Çizim arayüzünün (Overlay) hazırlanması
    # UYARI: Oyun içi grafik ayarlarınız "Pencereli" veya "Pencereli Tam Ekran" modunda olmalıdır.
    pyMeow.overlay_init("Counter-Strike 2", fps=60)
    
    color_t = pyMeow.rgb("red")
    color_ct = pyMeow.rgb("blue")
    color_health = pyMeow.rgb("green")

    print("ESP Sistemi Aktif Edildi. Kapatmak icin F1 tusuna basabilirsiniz.")

    while pyMeow.overlay_loop():
        if pyMeow.get_key_state(0x70): # F1 tuşu ile döngüyü sonlandırma
            break

        pyMeow.begin_drawing()

        try:
            # Güncel adres girdilerinden matris ve yerel oyuncu verilerini çekme
            view_matrix = pm.read_bytes(client + offsets["dwViewMatrix"], 64)
            entity_list = pm.read_longlong(client + offsets["dwEntityList"])
            local_player = pm.read_longlong(client + offsets["dwLocalPlayerPawn"])

            if not entity_list or not local_player:
                pyMeow.end_drawing()
                continue

            local_team = pm.read_int(local_player + schemas["m_iTeamNum"])
        except Exception:
            pyMeow.end_drawing()
            continue

        # 64 Oyuncu slotunun hafıza üzerinden filtrelenmesi
        for i in range(1, 64):
            try:
                # 1. Katman Sayfalama Mantığı
                list_entity = pm.read_longlong(entity_list + (8 * (i >> 9)) + 0x10)
                if not list_entity: 
                    continue

                # 2. Katman Controller Adreslemesi (0x70 / 112 Çarpanı)
                entity_controller = pm.read_longlong(list_entity + 0x70 * (i & 0x1FF))
                if not entity_controller: 
                    continue

                # Takım Ayrımı
                team = pm.read_int(entity_controller + schemas["m_iTeamNum"])
                if team == local_team: 
                    continue 

                # Pawn Handle Okuma işlemi
                pawn_handle = pm.read_uint(entity_controller + schemas["m_hPlayerPawn"])
                if not pawn_handle: 
                    continue

                # Handle üzerinden gerçek harita nesnesine erişim
                pawn_list_entity = pm.read_longlong(entity_list + (8 * ((pawn_handle & 0x7FFF) >> 9)) + 0x10)
                if not pawn_list_entity: 
                    continue

                player_pawn = pm.read_longlong(pawn_list_entity + 0x70 * (pawn_handle & 0x1FF))
                if not player_pawn or player_pawn == local_player: 
                    continue

                # Can (Health) Bilgisi Doğrulaması
                health = pm.read_int(player_pawn + schemas["m_iHealth"])
                if health <= 0 or health > 100: 
                    continue

                # 3 Boyutlu Uzay Koordinatlarının Çekilmesi
                x = pm.read_float(player_pawn + schemas["m_vOldOrigin"])
                y = pm.read_float(player_pawn + schemas["m_vOldOrigin"] + 4)
                z = pm.read_float(player_pawn + schemas["m_vOldOrigin"] + 8)

                # World to Screen Dönüşüm Algoritması
                try:
                    screen_foot = pyMeow.world_to_screen(view_matrix, [x, y, z])
                    screen_head = pyMeow.world_to_screen(view_matrix, [x, y, z + 72.0]) 

                    if screen_foot and screen_head:
                        head_y = screen_head["y"]
                        foot_y = screen_foot["y"]
                        
                        # Geometrik Kutu Ölçeklendirmesi
                        height = foot_y - head_y
                        width = height / 2
                        box_x = screen_foot["x"] - (width / 2)

                        # Düşman Ekip Rengine Göre Kutu Çizimi
                        box_color = color_t if team == 2 else color_ct
                        pyMeow.draw_rectangle_lines(box_x, head_y, width, height, box_color, 2.0)

                        # Orantılı Can Barı Göstergesi
                        health_height = (height * health) / 100
                        pyMeow.draw_rectangle(box_x - 6, foot_y - health_height, 4, health_height, color_health)
                except Exception:
                    pass

            except Exception:
                continue

        pyMeow.end_drawing()
        time.sleep(0.001)

if __name__ == "__main__":
    main()
              
