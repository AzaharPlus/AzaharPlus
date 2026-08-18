// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <optional>
#include <sstream>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/file_sys/certificate.h"
#include "core/file_sys/otp.h"
#include "core/hle/service/fs/archive.h"
#include "core/hw/aes/arithmetic128.h"
#include "core/hw/aes/key.h"
#ifdef ENABLE_BUILTIN_KEYBLOB
#include "core/hw/default_keys.h"
#endif // ENABLE_BUILTIN_KEYBLOB
#include "core/hw/rsa/rsa.h"
#include "core/loader/loader.h"

namespace HW::AES {

namespace {

// The generator constant was calculated using the 0x39 KeyX and KeyY retrieved from a 3DS and the
// normal key dumped from a Wii U solving the equation:
// NormalKey = (((KeyX ROL 2) XOR KeyY) + constant) ROL 87
// On a real 3DS the generation for the normal key is hardware based, and thus the constant can't
// get dumped. Generated normal keys are also not accessible on a 3DS. The used formula for
// calculating the constant is a software implementation of what the hardware generator does.
AESKey generator_constant;

//constexpr AESKey generator_constant = {{0x1F, 0xF9, 0xE9, 0xAA, 0xC5, 0xFE, 0x04, 0x08, 0x02, 0x45,
//                                        0x91, 0xDC, 0x5D, 0x52, 0x76, 0x8A}};

AESKey HexToKey(const std::string& hex) {
    if (hex.size() < 32) {
        throw std::invalid_argument("hex string is too short");
    }

    AESKey key;
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<u8>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
    }

    return key;
}

std::vector<u8> HexToVector(const std::string& hex) {
    std::vector<u8> vector(hex.size() / 2);
    for (std::size_t i = 0; i < vector.size(); ++i) {
        vector[i] = static_cast<u8>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
    }

    return vector;
}

std::optional<std::size_t> ParseCommonKeyName(const std::string& full_name) {
    std::size_t index;
    int end;
    if (std::sscanf(full_name.c_str(), "common%zd%n", &index, &end) == 1 &&
        end == static_cast<int>(full_name.size())) {
        return index;
    } else {
        return std::nullopt;
    }
}

std::optional<std::pair<std::size_t, std::string>> ParseNfcSecretName(
    const std::string& full_name) {
    std::size_t index;
    int end;
    if (std::sscanf(full_name.c_str(), "nfcSecret%zd%n", &index, &end) == 1) {
        return std::make_pair(index, full_name.substr(end));
    } else {
        return std::nullopt;
    }
}

std::optional<std::pair<std::size_t, char>> ParseKeySlotName(const std::string& full_name) {
    std::size_t slot;
    char type;
    int end;
    if (std::sscanf(full_name.c_str(), "slot0x%zXKey%c%n", &slot, &type, &end) == 2 &&
        end == static_cast<int>(full_name.size())) {
        return std::make_pair(slot, type);
    } else {
        return std::nullopt;
    }
}

struct KeySlot {
    std::optional<AESKey> x;
    std::optional<AESKey> y;
    std::optional<AESKey> normal;

    void SetKeyX(std::optional<AESKey> key) {
        x = key;
        GenerateNormalKey();
    }

    void SetKeyY(std::optional<AESKey> key) {
        y = key;
        GenerateNormalKey();
    }

    void SetNormalKey(std::optional<AESKey> key) {
        normal = key;
    }

    void GenerateNormalKey() {
        if (x && y) {
            normal = Lrot128(Add128(Xor128(Lrot128(*x, 2), *y), generator_constant), 87);
        } else {
            normal.reset();
        }
    }

    void Clear() {
        x.reset();
        y.reset();
        normal.reset();
    }
};

std::array<KeySlot, KeySlotID::MaxKeySlotID> key_slots;
std::array<std::optional<AESKey>, MaxCommonKeySlot> common_key_y_slots;
std::array<std::optional<AESKey>, NumDlpNfcKeyYs> dlp_nfc_key_y_slots;
std::array<NfcSecret, NumNfcSecrets> nfc_secrets;
AESIV nfc_iv;

AESKey otp_key{};
AESIV otp_iv{};

// gets xor'd with the mac address to produce the final iv
AESIV dlp_checksum_mod_iv;

KeySlot movable_key;
KeySlot movable_cmac;

struct KeyDesc {
    char key_type;
    std::size_t slot_id;
    // This key is identical to the key with the same key_type and slot_id -1
    bool same_as_before;
};

void LoadBootromKeys() {
    constexpr std::array<KeyDesc, 80> keys = {
        {{'X', 0x2C, false}, {'X', 0x2D, true},  {'X', 0x2E, true},  {'X', 0x2F, true},
         {'X', 0x30, false}, {'X', 0x31, true},  {'X', 0x32, true},  {'X', 0x33, true},
         {'X', 0x34, false}, {'X', 0x35, true},  {'X', 0x36, true},  {'X', 0x37, true},
         {'X', 0x38, false}, {'X', 0x39, true},  {'X', 0x3A, true},  {'X', 0x3B, true},
         {'X', 0x3C, false}, {'X', 0x3D, false}, {'X', 0x3E, false}, {'X', 0x3F, false},
         {'Y', 0x4, false},  {'Y', 0x5, false},  {'Y', 0x6, false},  {'Y', 0x7, false},
         {'Y', 0x8, false},  {'Y', 0x9, false},  {'Y', 0xA, false},  {'Y', 0xB, false},
         {'N', 0xC, false},  {'N', 0xD, true},   {'N', 0xE, true},   {'N', 0xF, true},
         {'N', 0x10, false}, {'N', 0x11, true},  {'N', 0x12, true},  {'N', 0x13, true},
         {'N', 0x14, false}, {'N', 0x15, false}, {'N', 0x16, false}, {'N', 0x17, false},
         {'N', 0x18, false}, {'N', 0x19, true},  {'N', 0x1A, true},  {'N', 0x1B, true},
         {'N', 0x1C, false}, {'N', 0x1D, true},  {'N', 0x1E, true},  {'N', 0x1F, true},
         {'N', 0x20, false}, {'N', 0x21, true},  {'N', 0x22, true},  {'N', 0x23, true},
         {'N', 0x24, false}, {'N', 0x25, true},  {'N', 0x26, true},  {'N', 0x27, true},
         {'N', 0x28, true},  {'N', 0x29, false}, {'N', 0x2A, false}, {'N', 0x2B, false},
         {'N', 0x2C, false}, {'N', 0x2D, true},  {'N', 0x2E, true},  {'N', 0x2F, true},
         {'N', 0x30, false}, {'N', 0x31, true},  {'N', 0x32, true},  {'N', 0x33, true},
         {'N', 0x34, false}, {'N', 0x35, true},  {'N', 0x36, true},  {'N', 0x37, true},
         {'N', 0x38, false}, {'N', 0x39, true},  {'N', 0x3A, true},  {'N', 0x3B, true},
         {'N', 0x3C, true},  {'N', 0x3D, false}, {'N', 0x3E, false}, {'N', 0x3F, false}}};

    // Bootrom sets all these keys when executed, but later some of the normal keys get overwritten
    // by other applications e.g. process9. These normal keys thus aren't used by any application
    // and have no value for emulation

    const std::string filepath = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + BOOTROM9;
    auto file = FileUtil::IOFile(filepath, "rb");
    if (!file) {
        return;
    }

    const std::size_t length = file.GetSize();
    if (length != 65536) {
        LOG_ERROR(HW_AES, "Bootrom9 size is wrong: {}", length);
        return;
    }

    constexpr std::size_t KEY_SECTION_START = 55760;
    file.Seek(KEY_SECTION_START, SEEK_SET); // Jump to the key section

    AESKey new_key;
    for (const auto& key : keys) {
        if (!key.same_as_before) {
            file.ReadArray(new_key.data(), new_key.size());
            if (!file) {
                LOG_ERROR(HW_AES, "Reading from Bootrom9 failed");
                return;
            }
        }

        LOG_DEBUG(HW_AES, "Loaded Slot{:#02x} Key{} from Bootrom9.", key.slot_id, key.key_type);

        switch (key.key_type) {
        case 'X':
            key_slots.at(key.slot_id).SetKeyX(new_key);
            break;
        case 'Y':
            key_slots.at(key.slot_id).SetKeyY(new_key);
            break;
        case 'N':
            key_slots.at(key.slot_id).SetNormalKey(new_key);
            break;
        default:
            LOG_ERROR(HW_AES, "Invalid key type {}", key.key_type);
            break;
        }
    }
}

void LoadPresetKeys() {
    auto s = GetKeysStream();

    std::string mode = "";

    while (!s.eof()) {
        std::string line;
        std::getline(s, line);
		
	//	LOG_ERROR(HW_AES, "Dump key '{}'", line);

        // Ignore empty or commented lines.
        if (line.empty() || line.starts_with("#")) {
            continue;
        }

        if (line.starts_with(":")) {
            mode = line.substr(1);
            continue;
        }

        if (mode != "AES") {
            continue;
        }

        const auto parts = Common::SplitString(line, '=');
        if (parts.size() != 2) {
            LOG_ERROR(HW_AES, "Failed to parse {}", line);
            continue;
        }

        const std::string& name = parts[0];

        const auto nfc_secret = ParseNfcSecretName(name);
        if (nfc_secret) {
            auto value = HexToVector(parts[1]);
            if (nfc_secret->first >= nfc_secrets.size()) {
                LOG_ERROR(HW_AES, "Invalid NFC secret index {}", nfc_secret->first);
            } else if (nfc_secret->second == "Phrase") {
                nfc_secrets[nfc_secret->first].phrase = value;
            } else if (nfc_secret->second == "Seed") {
                nfc_secrets[nfc_secret->first].seed = value;
            } else if (nfc_secret->second == "HmacKey") {
                nfc_secrets[nfc_secret->first].hmac_key = value;
            } else {
                LOG_ERROR(HW_AES, "Invalid NFC secret '{}'", name);
            }
            continue;
        }

        AESKey key;
        try {
            key = HexToKey(parts[1]);
        } catch (const std::logic_error& e) {
            LOG_ERROR(HW_AES, "Invalid key {}: {}", parts[1], e.what());
            continue;
        }

        const auto common_key = ParseCommonKeyName(name);
        if (common_key) {
            if (common_key >= common_key_y_slots.size()) {
                LOG_ERROR(HW_AES, "Invalid common key index {}", common_key.value());
            } else {
                common_key_y_slots[common_key.value()] = key;
            }
            continue;
        }

        if (name == "generatorConstant") {
            generator_constant = key;
            continue;
        }

        if (name == "otpKey") {
            otp_key = key;
            continue;
        }

        if (name == "otpIV") {
            otp_iv = key;
            continue;
        }

        if (name == "movableKeyY") {
            movable_key.SetKeyY(key);
            continue;
        }

        if (name == "movableCmacY") {
            movable_cmac.SetKeyY(key);
            continue;
        }

        if (name == "dlpKeyY") {
            dlp_nfc_key_y_slots[DlpNfcKeyY::Dlp] = key;
            continue;
        }

        if (name == "nfcKeyY") {
            dlp_nfc_key_y_slots[DlpNfcKeyY::Nfc] = key;
            continue;
        }

        if (name == "nfcIv") {
            nfc_iv = key;
            continue;
        }

        if (name == "dlpChecksumModIv") {
            dlp_checksum_mod_iv = key;
            continue;
        }

        const auto key_slot = ParseKeySlotName(name);
        if (!key_slot) {
            LOG_ERROR(HW_AES, "Invalid key name '{}'", name);
            continue;
        }

        if (key_slot->first >= MaxKeySlotID) {
            LOG_ERROR(HW_AES, "Out of range key slot ID {:#X}", key_slot->first);
            continue;
        }

        switch (key_slot->second) {
        case 'X':
            key_slots.at(key_slot->first).SetKeyX(key);
            break;
        case 'Y':
            key_slots.at(key_slot->first).SetKeyY(key);
            break;
        case 'N':
            key_slots.at(key_slot->first).SetNormalKey(key);
            break;
        default:
            LOG_ERROR(HW_AES, "Invalid key type '{}'", key_slot->second);
            break;
        }
    }
}

void LoadPresetAesKeys() {
    const std::string filepath = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + AES_KEYS;
    FileUtil::CreateFullPath(filepath); // Create path if not already created

    boost::iostreams::stream<boost::iostreams::file_descriptor_source> file;
    FileUtil::OpenFStream<std::ios_base::in>(file, filepath);
    if (!file.is_open()) {
        return;
    }

    while (!file.eof()) {
        std::string line;
        std::getline(file, line);

        // Ignore empty or commented lines.
        if (line.empty() || line.starts_with("#")) {
            continue;
        }

        const auto parts = Common::SplitString(line, '=');
        if (parts.size() != 2) {
            LOG_ERROR(HW_AES, "Failed to parse {}", line);
            continue;
        }

        const std::string& name = parts[0];

        const auto nfc_secret = ParseNfcSecretName(name);
        if (nfc_secret) {
            auto value = HexToVector(parts[1]);
            if (nfc_secret->first >= nfc_secrets.size()) {
                LOG_ERROR(HW_AES, "Invalid NFC secret index {}", nfc_secret->first);
            } else if (nfc_secret->second == "Phrase") {
                nfc_secrets[nfc_secret->first].phrase = value;
            } else if (nfc_secret->second == "Seed") {
                nfc_secrets[nfc_secret->first].seed = value;
            } else if (nfc_secret->second == "HmacKey") {
                nfc_secrets[nfc_secret->first].hmac_key = value;
            } else {
                LOG_ERROR(HW_AES, "Invalid NFC secret '{}'", name);
            }
            continue;
        }

        AESKey key;
        try {
            key = HexToKey(parts[1]);
        } catch (const std::logic_error& e) {
            LOG_ERROR(HW_AES, "Invalid key {}: {}", parts[1], e.what());
            continue;
        }

        const auto common_key = ParseCommonKeyName(name);
        if (common_key) {
            if (common_key >= common_key_y_slots.size()) {
                LOG_ERROR(HW_AES, "Invalid common key index {}", common_key.value());
            } else {
                common_key_y_slots[common_key.value()] = key;
            }
            continue;
        }

        if (name == "dlpKeyY") {
            dlp_nfc_key_y_slots[DlpNfcKeyY::Dlp] = key;
            continue;
        }

        if (name == "nfcKeyY") {
            dlp_nfc_key_y_slots[DlpNfcKeyY::Nfc] = key;
            continue;
        }

        if (name == "nfcIv") {
            nfc_iv = key;
            continue;
        }

        const auto key_slot = ParseKeySlotName(name);
        if (!key_slot) {
            LOG_ERROR(HW_AES, "Invalid key name '{}'", name);
            continue;
        }

        if (key_slot->first >= MaxKeySlotID) {
            LOG_ERROR(HW_AES, "Out of range key slot ID {:#X}", key_slot->first);
            continue;
        }

        switch (key_slot->second) {
        case 'X':
            key_slots.at(key_slot->first).SetKeyX(key);
            break;
        case 'Y':
            key_slots.at(key_slot->first).SetKeyY(key);
            break;
        case 'N':
            key_slots.at(key_slot->first).SetNormalKey(key);
            break;
        default:
            LOG_ERROR(HW_AES, "Invalid key type '{}'", key_slot->second);
            break;
        }
    }
}

} // namespace

std::istringstream GetKeysStream() {
    const std::string filepath = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + KEYS_FILE;
    FileUtil::CreateFullPath(filepath); // Create path if not already created

    boost::iostreams::stream<boost::iostreams::file_descriptor_source> file;
    FileUtil::OpenFStream<std::ios_base::in>(file, filepath);
    std::istringstream ret;
    if (file.is_open()) {
        return std::istringstream(std::string(std::istreambuf_iterator<char>(file), {}));
    } else {
#ifdef ENABLE_BUILTIN_KEYBLOB
        // The key data is encrypted in the source to prevent easy access to it for unintended
        // purposes.
        /*
        Here it is in clear because obfuscating stuff in an open source project is unhinged:
:AES
# Generator constant
generatorConstant=1ff9e9aac5fe0408024591dc5d52768a
# OTP
otpKey=06457901d485a367ac4f2ad01c53cf74
otpIV=ba4f599b0ae1122c80e13f6865c4fa49
# Movable
movableKeyY=a717802dea9776137ba16cd389141cf0
movableCmacY=6e1e6bcb9ee098dd67ae771ad73b2cc9
# KeyX
slot0x18KeyX=82e9c9bebfb8bdb875ecc0a07d474374
slot0x19KeyX=f5367fce73142e66ed13917914b7f2ef
slot0x1AKeyX=eaba984c9cb766d4a3a7e974e2e713a3
slot0x1BKeyX=45ad04953992c7c893724a9a7bce6182
slot0x1CKeyX=c3830f8156e3543b723f0bc046741e8f
slot0x1DKeyX=d6b38bc759417596d619d6029d13e0d8
slot0x1EKeyX=bb623a97ddd793d757c4104b8d9fb969
slot0x1FKeyX=4c28ec6effa3c23646078bba350c7995
slot0x25KeyX=cee7d8ab30c00dae850ef5e382ac5af3
slot0x2CKeyX=b98e95ceca3e4d171f76a94de934c053
slot0x2DKeyX=b98e95ceca3e4d171f76a94de934c053
slot0x2EKeyX=b98e95ceca3e4d171f76a94de934c053
slot0x2FKeyX=b98e95ceca3e4d171f76a94de934c053
slot0x30KeyX=c66e23128f289133f04cdb877a3749f2
slot0x31KeyX=c66e23128f289133f04cdb877a3749f2
slot0x32KeyX=c66e23128f289133f04cdb877a3749f2
slot0x33KeyX=c66e23128f289133f04cdb877a3749f2
slot0x34KeyX=6fbb01f872caf9c01834eec04065ee53
slot0x35KeyX=6fbb01f872caf9c01834eec04065ee53
slot0x36KeyX=6fbb01f872caf9c01834eec04065ee53
slot0x37KeyX=6fbb01f872caf9c01834eec04065ee53
slot0x38KeyX=b529221cddb5db5a1bf26eff2041e875
slot0x39KeyX=b529221cddb5db5a1bf26eff2041e875
slot0x3AKeyX=b529221cddb5db5a1bf26eff2041e875
slot0x3BKeyX=b529221cddb5db5a1bf26eff2041e875
slot0x3CKeyX=c35d6d15680b1ad4e912a341836121b3
slot0x3DKeyX=617085719b7cfb316df4df2e8362c6e2
slot0x3EKeyX=24baf628d06889bf282d0aa35dc55650
slot0x3FKeyX=a31233280bb4daa7761393f78c424952
# KeyY
slot0x04KeyY=ff3388ecd21705bb339e967986dc4907
slot0x05KeyY=54ef035f30260e0e9b5e004fc985dc22
slot0x06KeyY=24b05aaaac0b099252030c02d1040317
slot0x07KeyY=e9acc5abd4ad3f0660c83c8934882f3f
slot0x08KeyY=4803050106d482dcd75f85c5aadff9b3
slot0x09KeyY=af6346efdddfa9806e3c6b6855b78930
slot0x0AKeyY=0a870a2c4b2fc3172e5f0335d8c5085d
slot0x0BKeyY=fda0152fcd6ddb3133b887ba727c0ada
slot0x24KeyY=74ca074884f4228deb2a1ca72d287762
slot0x2EKeyY=7462553f9e5a7904b8647cca736da1f5
slot0x2FKeyY=c369baa21e188a88a9aa94e5506a9f16
slot0x31KeyY=7462553f9e5a7904b8647cca736da1f5
# DLP/NFC KeyY (slot 0x39)
dlpKeyY=7462553f9e5a7904b8647cca736da1f5
nfcKeyY=ed7858a8bba7eed7fc970c5979bc0af2
# Ticket Common KeyY (slot 0x3D)
common0=d07b337f9ca4385932a2e25723232eb9
common1=0c767230f0998f1c46828202faacbe4c
common2=c475cb3ab8c788bb575e12a10907b8a4
common3=e486eee3d0c09c902f6686d4c06f649f
common4=ed31ba9c04b067506c4497a35b7804fc
common5=5e66998ab4e8931606850fd7a16dd755
# KeyN
slot0x0CKeyN=e7c9ff9d4f5b6f4dc5e2f50e856f0ab2
slot0x0DKeyN=e7c9ff9d4f5b6f4dc5e2f50e856f0ab2
slot0x0EKeyN=e7c9ff9d4f5b6f4dc5e2f50e856f0ab2
slot0x0FKeyN=e7c9ff9d4f5b6f4dc5e2f50e856f0ab2
slot0x10KeyN=285713db53051c089bdfb3b6aa638fda
slot0x11KeyN=285713db53051c089bdfb3b6aa638fda
slot0x12KeyN=285713db53051c089bdfb3b6aa638fda
slot0x13KeyN=285713db53051c089bdfb3b6aa638fda
slot0x14KeyN=2af3bbd32cd59c06fd4abe58651987ad
slot0x15KeyN=be2836751c734ba8da18e1887f888bd6
slot0x16KeyN=e3a18eb1c1dc8a3d27c3967e6e362de3
slot0x17KeyN=d0294cfb7be0b4fb7324d986fd3993bb
slot0x20KeyN=7c92f6272551c4614db0b345edd2e869
slot0x21KeyN=7c92f6272551c4614db0b345edd2e869
slot0x22KeyN=7c92f6272551c4614db0b345edd2e869
slot0x23KeyN=7c92f6272551c4614db0b345edd2e869
slot0x26KeyN=bbe8b4e09d0937816b234d8eb3cd3ca2
slot0x27KeyN=bbe8b4e09d0937816b234d8eb3cd3ca2
slot0x28KeyN=bbe8b4e09d0937816b234d8eb3cd3ca2
slot0x29KeyN=5218127e133ce3b85bb8c018ce76b7e2
slot0x2AKeyN=4a4264cf32e84170666f29ac88ef3f7e
slot0x2BKeyN=51af6c4c8b13da3228bd29b371cf84e1
slot0x2DKeyN=3ed6f5cf2cc37c54655000b7c8b52e0d
slot0x32KeyN=b87e64018b190ffe048a8124c6454196
slot0x36KeyN=28c0d59b736657bcdf50ff174979958a
slot0x38KeyN=6e78a3be9bddda09bfd569483f24fce0
# NFC Secrets
nfcSecret0Phrase=756e666978656420696e666f7300
nfcSecret0Seed=db4b9e3f45278f397eff9b4fb993
nfcSecret0HmacKey=1d164b375b72a55728b91d64b6a3c205
nfcSecret1Phrase=6c6f636b65642073656372657400
nfcSecret1Seed=fdc8a07694b89e4c47d37de8ce5c74c1
nfcSecret1HmacKey=7f752d2873a20017fef85c0575904b6d
nfcIv=4fd39a6e79fceaad99904db8ee38e9db
# DLP Checksum
dlpChecksumModIv=fe449ac13ae3b4095011d18944107833
:RSA
# Slots
slot0x00X=c034829a11c7116a08633e89a78ca0919779da8cc967077afc60e1786247e5068b9a76588a15f0304b3887b5147c259f7a2e6e480aceb0be35f0c5885ea2d8838fd6aafe45ac58fc08d4d0569d3cd3b09f9c6985fcd5c7152ebc54a885d6b11129b503611510bbfa0b07552d5800ecf636ef90d101cd475a3f4274e0c1e828b2bb516c8cd80f95f5db6a239083435509c0190e5d218b19c3bebc2a3aa73cebf53e7e4998d9be01a440f3f1c0f3157ba6f65a7be9e059d4d26f9de997141979df2ceaa175d91db2079051978f805d6f97741b6db4e96e3305611392b4b7ca76f0b45387c40d5879cde6b1509062216cb9ac21f3510ccd6df4da8f581ff86d2f31
slot0x00M=c12e4877ff0fedb98afa6dbe5d7ac86489a4e0901dba22d2e7ba945c830126ca71976f284a5e57e386b4778fbb78e3f1750ecd8b6ed3984f3cd363f801dd9ed5271ea859f49ff4da02d88445efba774b3a46279167cb905300d0903ecb71772b78cfe918921e2bef699f87d9cbef273fbc0e320d177e925021e869bb827f227f17da206ae9ce5de262566c4fab0be86866f7218fd48c248c0d8cbdeb3adbc423a6cd1b572f82c0172d23e32b1b4f0c30b04c20749126851dba39b31a2918c23f91034969e601c0a1099742e8fd31cfe4a02cd9e6bd7b646c52465567c7463b2a9e7e9fa53a0e260d4daee68091ee7adfa1c1491538276161e1c10293f4592a33
# Ticket wrap
ticketWrapExp=010001
ticketWrapMod=d24cb2e48feaf004d4bb08f8f3defcbb0c934a146b15366c9ddc1eb1649b9feb964b569c2283954d3d2b8a1ab21dc1159c2e6cb4cdd4c0bb96dbad4f02d31f45573892af855273aca20c459b9bd3126425c05d766bfd2fad87986c08416aea8d4266cd9d4ffc3f20f7b5672b686793141edde1b11689aca2f6469c9b0ea4577150235185ed4e7e4f2f9036c165a20c73e160c644a47303d2ed9bb0ba2fc90989bd87eb4563d8f7a61d889a7807b155e7f2107d048d828fcba23090839341385614fde4fabe84f2f0532da34750f32ab11fbe084c0083edbf0b50e96a49dd9d1e293e2249ee954db8afd139461ead4f11a2d06836446473140ed3867e4e5ead3b
# Secure info
secureInfoExp=010001
secureInfoMod=b1791a6d1eadd429ba89a1cd433630174bc68730c5e70560197b50d8c4546710a6e8a101bc2ceb0376f005c70ce0b6d6dffd26df33468bdbb2391e7ec01aa1a5a091e807da378676ba390a25429d5961e161d40485a74bb20186beb11a3572c1c2ea28ab7a1015325c9e712b7df965eae6c6fb8baed76c2a94a6c5ece40eaf987e06f20f884fd20635a476e9f70aba5c5b1461520054044593e468270435355aad5809d1193f5a0728d6db6b551f77945dc3be6fae5bcc0863e476dfa29b36ea853403e616eaa905e07f3a3e7e7077cf166a61d17e4d354c744485d4f67b0eee32f1c2d5790248e9621a33baa39b02b02294057ff6b43888e301e55a237c9c0b
# Local friend code seed
lfcsExp=010001
lfcsMod=a3759a3546cfa7fe30ec55a1b64e08e9449d0c72fcd191fd610a288975bce6a9b21556e9c7670255adfc3cee5edb78259a4b221b71e7e9515b2a6793b21868ce5e5e12ffd86806af318d56f9549902346a17e7837496a05aaf6efde6bed686aafd7a65a8ebe11c983a15c17ab540c23d9b7cfdd463c5e6deb77824c629473335b2e937e054ee9fa53dd793ca3eae4db60f5a11e70cdfba03b21e2b31b65906db5f940bf76e74cad4ab55d940058f10fe06050c81bb422190ba4f5c5382e1e10fbc949f60695d1303aae2e0c108424c200b9baa552d55276e24e5d60457588ff75f0cec819f6d2d28f31055f83b7662d4e4a69369b5da6b4023af07eb9cbfa9c9
:ECC
# Root public
rootPublicXY=004e3bb74d5d959e68ce900434fe9e4a3f094a33771fa7c0e4b023264d98014ca1fc799d3fa52171d5f9bd5b1777ec0fef7a38d1669bbf830325843a
        */
        std::vector<u8> kiv(16);
        std::string s(default_keys_enc_size, ' ');
        CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption(kiv.data(), kiv.size(), kiv.data())
            .ProcessData(reinterpret_cast<u8*>(s.data()), default_keys_enc, s.size());
        return std::istringstream(s);
#else
        return std::istringstream("");
#endif // ENABLE_BUILTIN_KEYBLOB
    }
}

void InitKeys(bool force) {
    static bool initialized = false;
    if (initialized && !force) {
        return;
    }
    initialized = true;
    HW::RSA::InitSlots();
    LoadBootromKeys();
	generator_constant = HexToKey("1ff9e9aac5fe0408024591dc5d52768a");
    LoadPresetKeys();
    LoadPresetAesKeys();
    movable_key.SetKeyX(key_slots[0x35].x);
    movable_cmac.SetKeyX(key_slots[0x35].x);

    HW::RSA::InitSlots();
    HW::ECC::InitSlots();
}

void SetKeyX(std::size_t slot_id, const AESKey& key) {
    key_slots.at(slot_id).SetKeyX(key);
}

void SetKeyY(std::size_t slot_id, const AESKey& key) {
    key_slots.at(slot_id).SetKeyY(key);
}

void SetNormalKey(std::size_t slot_id, const AESKey& key) {
    key_slots.at(slot_id).SetNormalKey(key);
}

bool IsKeyXAvailable(std::size_t slot_id) {
    return key_slots.at(slot_id).x.has_value();
}

bool IsNormalKeyAvailable(std::size_t slot_id) {
    return key_slots.at(slot_id).normal.has_value();
}

AESKey GetNormalKey(std::size_t slot_id) {
    return key_slots.at(slot_id).normal.value_or(AESKey{});
}

void SelectCommonKeyIndex(u8 index) {
    key_slots[KeySlotID::TicketCommonKey].SetKeyY(common_key_y_slots.at(index));
}

void SelectDlpNfcKeyYIndex(u8 index) {
    key_slots[KeySlotID::DLPNFCDataKey].SetKeyY(dlp_nfc_key_y_slots.at(index));
}

bool NfcSecretsAvailable() {
    auto missing_secret =
        std::find_if(nfc_secrets.begin(), nfc_secrets.end(), [](auto& nfc_secret) {
            return nfc_secret.phrase.empty() || nfc_secret.seed.empty() ||
                   nfc_secret.hmac_key.empty();
        });
    SelectDlpNfcKeyYIndex(DlpNfcKeyY::Nfc);
    return IsNormalKeyAvailable(KeySlotID::DLPNFCDataKey) && missing_secret == nfc_secrets.end();
}

const NfcSecret& GetNfcSecret(NfcSecretId secret_id) {
    return nfc_secrets[secret_id];
}

const AESIV& GetNfcIv() {
    return nfc_iv;
}

std::pair<AESKey, AESIV> GetOTPKeyIV() {
    return {otp_key, otp_iv};
}

const AESKey& GetMovableKey(bool cmac_key) {
    return cmac_key ? movable_cmac.normal.value() : movable_key.normal.value();
}

const AESIV& GetDlpChecksumModIv() {
    return dlp_checksum_mod_iv;
}

} // namespace HW::AES
