// 16 default factory presets for Paradox (mapped 1:1 to QSPI preset slots 0..15)
#pragma once
#include <cstdint>

namespace paradox_dsp {

struct ChannelSnap {
    uint8_t var;
    uint8_t rnd;
    uint8_t bs1;
    uint8_t bs2;
};

struct FactorySnapshot {
    const char* name;
    ChannelSnap A;
    ChannelSnap P;
    ChannelSnap RM;
    ChannelSnap FM;
    ChannelSnap M;
    ChannelSnap DEL;
    float size;
    float asym;
    float decay;
    float hp;
    float lp;
    float master_db;
};

inline constexpr uint8_t kNumFactorySnapshots = 16u;

inline constexpr FactorySnapshot kFactorySnapshots[kNumFactorySnapshots] = {
    /* Slot  1 */
    {"K1 seg 1", {44u, 83u, 117u, 123u}, {46u, 78u, 22u, 75u}, {1u, 124u, 110u, 55u}, {0u, 33u, 35u, 82u}, {19u, 23u, 28u, 14u}, {34u, 91u, 85u, 107u}, 3.40f, 3.0f, 46.0f, 28.0f, 128.0f, -21.5f},
    /* Slot  2 */
    {"K1 seg 2", {18u, 0u, 36u, 82u}, {44u, 121u, 84u, 106u}, {68u, 127u, 14u, 71u}, {1u, 0u, 121u, 60u}, {24u, 100u, 8u, 68u}, {31u, 68u, 105u, 116u}, 3.34f, 0.6f, 53.0f, 9.0f, 118.0f, -21.5f},
    /* Slot  3 */
    {"K1 seg 3", {64u, 0u, 11u, 5u}, {46u, 65u, 75u, 38u}, {0u, 0u, 51u, 90u}, {0u, 19u, 111u, 56u}, {0u, 16u, 88u, 44u}, {40u, 102u, 105u, 52u}, 2.90f, 3.2f, 26.0f, 25.0f, 115.0f, -21.5f},
    /* Slot  4 */
    {"K2 seg 1", {39u, 0u, 36u, 82u}, {48u, 121u, 84u, 106u}, {117u, 127u, 14u, 71u}, {2u, 0u, 121u, 60u}, {18u, 74u, 97u, 113u}, {31u, 68u, 105u, 116u}, 1.13f, -0.9f, 52.0f, 0.0f, 124.0f, -21.5f},
    /* Slot  5 */
    {"K2 seg 2", {56u, 87u, 49u, 24u}, {51u, 58u, 66u, 33u}, {0u, 0u, 66u, 97u}, {10u, 48u, 89u, 108u}, {9u, 20u, 82u, 41u}, {31u, 52u, 96u, 48u}, 1.67f, -1.4f, 35.0f, 32.0f, 114.0f, -21.5f},
    /* Slot  6 */
    {"K2 seg 3", {56u, 87u, 49u, 24u}, {49u, 77u, 16u, 8u}, {0u, 0u, 66u, 97u}, {10u, 48u, 89u, 108u}, {9u, 20u, 82u, 41u}, {31u, 52u, 96u, 48u}, 1.32f, 1.7f, 31.0f, 38.0f, 130.0f, -21.5f},
    /* Slot  7 */
    {"K3 seg 1", {65u, 85u, 29u, 15u}, {17u, 21u, 30u, 15u}, {0u, 0u, 22u, 75u}, {16u, 16u, 52u, 90u}, {12u, 23u, 28u, 14u}, {45u, 94u, 126u, 63u}, 2.99f, -0.7f, 46.0f, 61.0f, 114.0f, -21.5f},
    /* Slot  8 */
    {"K3 seg 2", {64u, 0u, 11u, 5u}, {38u, 65u, 75u, 38u}, {0u, 0u, 51u, 90u}, {0u, 19u, 111u, 56u}, {0u, 16u, 88u, 44u}, {39u, 102u, 105u, 52u}, 2.90f, 3.2f, 26.0f, 25.0f, 115.0f, -21.5f},
    /* Slot  9 */
    {"K3 seg 3", {29u, 116u, 20u, 10u}, {52u, 96u, 95u, 48u}, {62u, 53u, 8u, 68u}, {0u, 0u, 24u, 76u}, {18u, 108u, 38u, 19u}, {61u, 54u, 83u, 105u}, 3.37f, -2.7f, 50.0f, 0.0f, 128.0f, -21.5f},
    /* Slot 10 */
    {"K4 seg 1", {34u, 86u, 31u, 79u}, {57u, 123u, 25u, 77u}, {25u, 0u, 1u, 0u}, {5u, 0u, 107u, 54u}, {42u, 0u, 38u, 83u}, {39u, 125u, 55u, 28u}, 1.04f, -1.9f, 26.0f, 24.0f, 118.0f, -21.5f},
    /* Slot 11 */
    {"K4 seg 2", {58u, 127u, 82u, 41u}, {21u, 46u, 16u, 72u}, {0u, 0u, 1u, 0u}, {10u, 46u, 12u, 70u}, {12u, 53u, 107u, 117u}, {23u, 0u, 82u, 105u}, 3.34f, 0.6f, 39.0f, 27.0f, 81.0f, -21.5f},
    /* Slot 12 */
    {"K4 seg 3", {29u, 127u, 82u, 41u}, {57u, 97u, 93u, 110u}, {0u, 0u, 1u, 0u}, {21u, 46u, 12u, 70u}, {12u, 53u, 107u, 117u}, {23u, 10u, 41u, 20u}, 3.34f, 0.6f, 39.0f, 43.0f, 130.0f, -21.5f},
    /* Slot 13 */
    {"K5 seg 1", {36u, 127u, 82u, 41u}, {43u, 104u, 79u, 103u}, {0u, 0u, 1u, 0u}, {20u, 45u, 55u, 92u}, {12u, 53u, 107u, 117u}, {23u, 10u, 41u, 20u}, 3.34f, 0.6f, 39.0f, 48.0f, 130.0f, -21.5f},
    /* Slot 14 */
    {"K5 seg 2", {65u, 0u, 36u, 82u}, {53u, 109u, 60u, 94u}, {94u, 125u, 30u, 79u}, {0u, 0u, 121u, 60u}, {57u, 74u, 97u, 113u}, {34u, 73u, 70u, 99u}, 4.00f, -3.9f, 53.0f, 0.0f, 95.0f, -21.5f},
    /* Slot 15 */
    {"K5 seg 3", {8u, 0u, 36u, 82u}, {32u, 109u, 60u, 94u}, {0u, 0u, 78u, 39u}, {2u, 18u, 80u, 40u}, {35u, 74u, 97u, 113u}, {26u, 0u, 67u, 97u}, 4.00f, -3.9f, 43.0f, 0.0f, 95.0f, -21.5f},
    /* Slot 16 */
    {"K6 seg 1", {53u, 0u, 36u, 82u}, {50u, 67u, 42u, 21u}, {46u, 12u, 91u, 45u}, {0u, 0u, 110u, 55u}, {24u, 55u, 77u, 39u}, {38u, 20u, 70u, 99u}, 3.94f, 3.9f, 59.0f, 17.0f, 120.0f, -21.5f},
};

} // namespace paradox_dsp
