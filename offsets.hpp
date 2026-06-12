
// Generated using https://github.com/a2x/cs2-dumper
// 2026-06-11 01:17:17.911313600 UTC

#pragma once

#include <cstddef>
#include <cstdint>

namespace cs2_dumper {
    namespace offsets {
        // Module: client.dll (Ana Bellek Konumları)
        namespace client_dll {
            constexpr std::ptrdiff_t dwCSGOInput = 0x2356240;
            constexpr std::ptrdiff_t dwEntityList = 0x24E76A0;
            constexpr std::ptrdiff_t dwGameEntitySystem = 0x24E76A0;
            constexpr std::ptrdiff_t dwGameEntitySystem_highestEntityIndex = 0x2090;
            constexpr std::ptrdiff_t dwGameRules = 0x2341158;
            constexpr std::ptrdiff_t dwGlobalVars = 0x20616D0;
            constexpr std::ptrdiff_t dwGlowManager = 0x233DF50;
            constexpr std::ptrdiff_t dwLocalPlayerController = 0x2320720;
            constexpr std::ptrdiff_t dwLocalPlayerPawn = 0x2341698;
            constexpr std::ptrdiff_t dwPlantedC4 = 0x234FF98;
            constexpr std::ptrdiff_t dwPrediction = 0x23415A0;
            constexpr std::ptrdiff_t dwSensitivity = 0x233EA68;
            constexpr std::ptrdiff_t dwSensitivity_sensitivity = 0x58;
            constexpr std::ptrdiff_t dwViewAngles = 0x23568C8;
            constexpr std::ptrdiff_t dwViewMatrix = 0x2346B30;
            constexpr std::ptrdiff_t dwViewRender = 0x2346EE0;
            constexpr std::ptrdiff_t dwWeaponC4 = 0x22BED20;
        }
    }

    // Class Schemas: client.dll (Sınıf İçi Değişken Konumları)
    namespace schemas {
        namespace client_dll {
            // CCSPlayerController Sınıfı Bilgileri
            namespace CCSPlayerController {
                constexpr std::ptrdiff_t m_sSanitizedPlayerName = 0x860; // Oyuncu İsmi (CUtlString)
                constexpr std::ptrdiff_t m_hPlayerPawn = 0x90C;          // Controller'dan Pawn nesnesine bağlanan Handle
                constexpr std::ptrdiff_t m_bPawnIsAlive = 0x914;         // Pawn Canlı mı Durumu (bool)
                constexpr std::ptrdiff_t m_iPawnHealth = 0x918;          // Pawn Sağlık Bilgisi (uint32)
            }

            // C_BaseEntity Sınıfı Bilgileri (Can, Takım ve Yaşam Durumu buradadır)
            namespace C_BaseEntity {
                constexpr std::ptrdiff_t m_iHealth = 0x34C;              // Can Bilgisi (int32)
                constexpr std::ptrdiff_t m_iTeamNum = 0x3EB;             // Takım Numarası (uint8)
                constexpr std::ptrdiff_t m_lifeState = 0x354;            // Yaşam Durumu (uint8 - 0: Canlı)
            }

            // C_BasePlayerPawn Sınıfı Bilgileri
            namespace C_BasePlayerPawn {
                constexpr std::ptrdiff_t m_vOldOrigin = 0x1390;          // 3D Dünya Koordinatları (Vector)
            }
        }
    }
}



