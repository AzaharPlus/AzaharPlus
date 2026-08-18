// Copyright 2025-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cryptopp/sha.h>
#include "common/common_paths.h"
#include "common/file_derived.h"
#include "common/logging/log.h"
#include "core/file_sys/archive_systemsavedata.h"
#include "core/file_sys/certificate.h"
#include "core/file_sys/ncch_container.h"
#include "core/file_sys/otp.h"
#include "core/hw/aes/key.h"
#include "core/hw/ecc.h"
#include "core/hw/rsa/rsa.h"
#include "core/hw/unique_data.h"
#include "core/loader/loader.h"
#include <map>
#include <sstream>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include "core/system_titles.h"

namespace HW::UniqueData {

static SecureInfoA secure_info_a;
static bool secure_info_a_signature_valid = false;
static bool secure_info_a_region_changed = false;
static LocalFriendCodeSeedB local_friend_code_seed_b;
static bool local_friend_code_seed_b_signature_valid = false;
static FileSys::OTP otp;
static FileSys::Certificate ct_cert;
static MovableSedFull movable;
static bool movable_signature_valid = false;
static std::mutex load_mutex;

static const unsigned char dummy_secure_info[sizeof(SecureInfoA)] = {
	0x44, 0x55, 0x4D, 0x4D, 0x59, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x0A, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x0A, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x0A, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x02, 0x00, 0x41, 0x42, 0x43, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x00, 0x00, 0x00, 
	0x00
};

static const unsigned char dummy_local_friend_code_seed[sizeof(LocalFriendCodeSeedB)] = {
	0x44, 0x55, 0x4D, 0x4D, 0x59, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x0A, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x0A, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x0A, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x31, 0x32, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const unsigned char dummy_movable[sizeof(MovableSedFull)] = {
	0x53, 0x45, 0x45, 0x44, 0x00, 0x01, 0x00, 0x00, 0x44, 0x55, 0x4D, 0x4D, 0x59, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x0A, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x0A, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x0A, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x31, 0x32, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 
	0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D
};

bool SecureInfoA::VerifySignature() const {
    return true;
    auto sec_info_slot = HW::RSA::GetSecureInfoSlot();
    return sec_info_slot &&
           sec_info_slot.Verify(
               std::span<const u8>(reinterpret_cast<const u8*>(&body), sizeof(body)), signature);
}

bool LocalFriendCodeSeedB::VerifySignature() const {
    return true;
    auto lfcs_slot = HW::RSA::GetLocalFriendCodeSeedSlot();
    return lfcs_slot &&
           HW::RSA::GetLocalFriendCodeSeedSlot().Verify(
               std::span<const u8>(reinterpret_cast<const u8*>(&body), sizeof(body)), signature);
}

bool MovableSed::VerifySignature() const {
	return true;
    return lfcs.VerifySignature();
}

SecureDataLoadStatus LoadSecureInfoA() {
    if (secure_info_a.IsValid()) {
        if (!HW::RSA::GetSecureInfoSlot()) {
            return SecureDataLoadStatus::CannotValidateSignature;
        }
        return secure_info_a_signature_valid
                   ? SecureDataLoadStatus::Loaded
                   : (secure_info_a_region_changed ? SecureDataLoadStatus::RegionChanged
                                                   : SecureDataLoadStatus::InvalidSignature);
    }
    std::string file_path = GetSecureInfoAPath();
    if (!FileUtil::Exists(file_path)) {
		if(Settings::values.enable_required_online_lle_modules.GetValue()){
        	memcpy(&secure_info_a, dummy_secure_info, sizeof(dummy_secure_info));
		} else return SecureDataLoadStatus::NotFound;
    } else {
		FileUtil::IOFile file(file_path, "rb");
		if (!file.IsOpen()) {
			return SecureDataLoadStatus::IOError;
		}
		if (file.GetSize() != sizeof(SecureInfoA)) {
			return SecureDataLoadStatus::Invalid;
		}
		if (file.ReadBytes(&secure_info_a, sizeof(SecureInfoA)) != sizeof(SecureInfoA)) {
			secure_info_a.Invalidate();
			return SecureDataLoadStatus::IOError;
		}
	}
	
    secure_info_a_region_changed = false;
    HW::AES::InitKeys();
    if (!HW::RSA::GetSecureInfoSlot()) {
        return SecureDataLoadStatus::CannotValidateSignature;
    }
    secure_info_a_signature_valid = secure_info_a.VerifySignature();
    if (!secure_info_a_signature_valid) {
        // Check if the file has been region changed
        SecureInfoA copy = secure_info_a;
        for (u8 orig_reg = 0; orig_reg < Region::COUNT; orig_reg++) {
            if (orig_reg == secure_info_a.body.region) {
                continue;
            }
            copy.body.region = orig_reg;
            if (copy.VerifySignature()) {
                secure_info_a_region_changed = true;
                LOG_WARNING(HW, "SecureInfo_A is region changed and its signature invalid");
                break;
            }
        }
        if (!secure_info_a_region_changed) {
            LOG_WARNING(HW, "SecureInfo_A signature check failed");
        }
    }

    return secure_info_a_signature_valid
               ? SecureDataLoadStatus::Loaded
               : (secure_info_a_region_changed ? SecureDataLoadStatus::RegionChanged
                                               : SecureDataLoadStatus::InvalidSignature);
}

SecureDataLoadStatus LoadLocalFriendCodeSeedB() {
    if (local_friend_code_seed_b.IsValid()) {
        if (!HW::RSA::GetLocalFriendCodeSeedSlot()) {
            return SecureDataLoadStatus::CannotValidateSignature;
        }
        return local_friend_code_seed_b_signature_valid ? SecureDataLoadStatus::Loaded
                                                        : SecureDataLoadStatus::InvalidSignature;
    }
    std::string file_path = GetLocalFriendCodeSeedBPath();
    if (!FileUtil::Exists(file_path)) {
        if(Settings::values.enable_required_online_lle_modules.GetValue()){
        	memcpy(&local_friend_code_seed_b, dummy_local_friend_code_seed, sizeof(dummy_local_friend_code_seed));
		} else return SecureDataLoadStatus::NotFound;
    } else {
		FileUtil::IOFile file(file_path, "rb");
		if (!file.IsOpen()) {
			return SecureDataLoadStatus::IOError;
		}
		if (file.GetSize() != sizeof(LocalFriendCodeSeedB)) {
			return SecureDataLoadStatus::Invalid;
		}
		if (file.ReadBytes(&local_friend_code_seed_b, sizeof(LocalFriendCodeSeedB)) !=
			sizeof(LocalFriendCodeSeedB)) {
			local_friend_code_seed_b.Invalidate();
			return SecureDataLoadStatus::IOError;
		}
	}

    HW::AES::InitKeys();
    if (!HW::RSA::GetLocalFriendCodeSeedSlot()) {
        return SecureDataLoadStatus::CannotValidateSignature;
    }
    local_friend_code_seed_b_signature_valid = local_friend_code_seed_b.VerifySignature();
    if (!local_friend_code_seed_b_signature_valid) {
        LOG_WARNING(HW, "LocalFriendCodeSeed_B signature check failed");
    }

    return local_friend_code_seed_b_signature_valid ? SecureDataLoadStatus::Loaded
                                                    : SecureDataLoadStatus::InvalidSignature;
}

SecureDataLoadStatus LoadOTP() {
	std::scoped_lock lock(load_mutex);
	
    if (otp.Valid()) {
        return SecureDataLoadStatus::Loaded;
    }

    auto is_all_zero = [](const auto& arr) {
        return std::all_of(arr.begin(), arr.end(), [](auto x) { return x == 0; });
    };

    const std::string filepath = GetOTPPath();

    HW::AES::InitKeys();
    auto otp_keyiv = HW::AES::GetOTPKeyIV();
    if (is_all_zero(otp_keyiv.first) || is_all_zero(otp_keyiv.second)) {
        return SecureDataLoadStatus::NoCryptoKeys;
    }

    auto loader_status = otp.Load(filepath, otp_keyiv.first, otp_keyiv.second);
    if (loader_status != Loader::ResultStatus::Success) {
        otp.Invalidate();
        ct_cert.Invalidate();
        return loader_status == Loader::ResultStatus::ErrorNotFound ? SecureDataLoadStatus::NotFound
                                                                    : SecureDataLoadStatus::Invalid;
    }

    constexpr const char* issuer_ret = "Nintendo CA - G3_NintendoCTR2prod";
    constexpr const char* issuer_dev = "Nintendo CA - G3_NintendoCTR2dev";
    std::array<u8, 0x40> issuer = {0};
    if (otp.IsDev()) {
        memcpy(issuer.data(), issuer_dev, strlen(issuer_dev));
    } else {
        memcpy(issuer.data(), issuer_ret, strlen(issuer_ret));
    }
    std::string name_str = fmt::format("CT{:08X}-{:02X}", otp.GetDeviceID(), otp.GetSystemType());
    std::array<u8, 0x40> name = {0};
    memcpy(name.data(), name_str.data(), name_str.size());

    ct_cert.BuildECC(issuer, name, otp.GetCTCertExpiration(),
                     HW::ECC::CreateECCPrivateKey(otp.GetCTCertPrivateKey(), true),
                     HW::ECC::CreateECCSignature(otp.GetCTCertSignature()));

    if (!ct_cert.VerifyMyself(HW::ECC::GetRootPublicKey())) {
        LOG_ERROR(HW, "CTCert failed verification");
    //    otp.Invalidate();
    //    ct_cert.Invalidate();
    //    return SecureDataLoadStatus::IOError;
    }

    return SecureDataLoadStatus::Loaded;
}

SecureDataLoadStatus LoadMovable() {
    if (movable.IsValid()) {
        if (!HW::RSA::GetLocalFriendCodeSeedSlot()) {
            return SecureDataLoadStatus::CannotValidateSignature;
        }
        return movable_signature_valid ? SecureDataLoadStatus::Loaded
                                       : SecureDataLoadStatus::InvalidSignature;
    }
    std::string file_path = GetMovablePath();
    if (!FileUtil::Exists(file_path)) {
        if(Settings::values.enable_required_online_lle_modules.GetValue()){
        	memcpy(&movable, dummy_movable, sizeof(dummy_movable));
		} else return SecureDataLoadStatus::NotFound;
    } else {
		FileUtil::IOFile file(file_path, "rb");
		if (!file.IsOpen()) {
			return SecureDataLoadStatus::IOError;
		}

		std::size_t size = file.GetSize();
		if (size != sizeof(MovableSedFull) && size != sizeof(MovableSed)) {
			return SecureDataLoadStatus::Invalid;
		}

		std::memset(&movable, 0, sizeof(movable));
		if (file.ReadBytes(&movable, size) != size) {
			movable.Invalidate();
			return SecureDataLoadStatus::IOError;
		}
	}

    HW::AES::InitKeys();
    if (!HW::RSA::GetLocalFriendCodeSeedSlot()) {
        return SecureDataLoadStatus::CannotValidateSignature;
    }
    movable_signature_valid = movable.VerifySignature();
    if (!movable_signature_valid) {
        LOG_WARNING(HW, "movable.sed signature check failed");
    }

    return movable_signature_valid ? SecureDataLoadStatus::Loaded
                                   : SecureDataLoadStatus::InvalidSignature;
}

std::string GetSecureInfoAPath() {
    return FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + "rw/sys/SecureInfo_A";
}

std::string GetLocalFriendCodeSeedBPath() {
    return FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + "rw/sys/LocalFriendCodeSeed_B";
}

std::string GetOTPPath() {
    return FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + "otp.bin";
}

std::string GetMovablePath() {
    return FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + "private/movable.sed";
}

SecureInfoA& GetSecureInfoA() {
    LoadSecureInfoA();

	std::string file_path = GetSecureInfoAPath();
    if (!FileUtil::Exists(file_path)) {		
		if(Settings::values.enable_required_online_lle_modules.GetValue()){
        	const auto current_region = Settings::values.region_value.GetValue();
			for (u32 region = 0; region < Core::NUM_SYSTEM_TITLE_REGIONS; region++) {
				if(region == 3 && current_region != 3) continue;
				const auto path = Core::GetHomeMenuNcchPath(region);
			
				if(!path.empty() && FileUtil::Exists(path))
				{
					secure_info_a.body.region = region;
				
					if(current_region == static_cast<int>(region))
					{
						break;
					}
				}
			}
		} else secure_info_a.Invalidate();
	}
	
    return secure_info_a;
}

LocalFriendCodeSeedB& GetLocalFriendCodeSeedB() {
    LoadLocalFriendCodeSeedB();

	std::string file_path = GetLocalFriendCodeSeedBPath();
    if (!FileUtil::Exists(file_path)) {
		if(!Settings::values.enable_required_online_lle_modules.GetValue())
		{
			local_friend_code_seed_b.Invalidate();
		}
	}
	
    return local_friend_code_seed_b;
}

FileSys::Certificate& GetCTCert() {
    LoadOTP();

    std::string file_path = GetOTPPath();
    if (!FileUtil::Exists(file_path)) {
		if(!Settings::values.enable_required_online_lle_modules.GetValue())
		{
			ct_cert.Invalidate();
		}
	}
	
    return ct_cert;
}

FileSys::OTP& GetOTP() {
    LoadOTP();

    std::string file_path = GetOTPPath();
    if (!FileUtil::Exists(file_path)) {
		if(!Settings::values.enable_required_online_lle_modules.GetValue())
		{
			otp.Invalidate();
		}
	}
	
    return otp;
}
MovableSedFull& GetMovableSed() {
    LoadMovable();

    std::string file_path = GetMovablePath();
    if (!FileUtil::Exists(file_path)) {
		if(!Settings::values.enable_required_online_lle_modules.GetValue())
		{
			movable.Invalidate();
		}
	}
	
    return movable;
}
void InvalidateSecureData() {
    secure_info_a.Invalidate();
    local_friend_code_seed_b.Invalidate();
    otp.Invalidate();
    ct_cert.Invalidate();
    movable.Invalidate();
}

static std::string binToHex(u8 bin[])
{
	std::string res = "";
	
	for(int i=0; i<32; i++)
	{
		std::string s = fmt::format("{:02x}", bin[i]);
		res += s;
	}
	
	return res;
}

static std::array<u8, 32> hexToBin(const std::string& hex) {
    std::array<u8, 32> bytes;

    for (unsigned int i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        bytes[i/2] = static_cast<u8>(std::strtol(byteString.c_str(), nullptr, 16));
    }
	
    return bytes;
}

static bool isHeaderReadable(NCCH_Header ncch_header)
{
	bool ret = true;
	
	if (FileUtil::MakeMagic('N', 'C', 'S', 'D') != ncch_header.magic
	&&  FileUtil::MakeMagic('N', 'C', 'C', 'H') != ncch_header.magic
	&&  memcmp("Z3DS", ncch_header.signature, 4) != 0
	&&  memcmp("NDHT", ncch_header.signature, 4) != 0
	&&  memcmp("dlplay", ncch_header.signature, 6) != 0
	&&  memcmp("NARC", ncch_header.signature + 128, 4) != 0
	&&  memcmp("DS INTERNET", ncch_header.signature, 11) != 0)
	{
		ret = false;
	}
	
	return ret;
}

static bool testDigest(std::string sdigest, const std::string& filename)
{
	u8 digest[CryptoPP::SHA256::DIGESTSIZE];
	memcpy(digest, hexToBin(sdigest).data(), 32);

	std::vector<u8> key(0x10);
    std::vector<u8> ctr(0x10);
    memcpy(key.data(), digest, 0x10);
    memcpy(ctr.data(), digest + 0x10, 12);

    FileUtil::CryptoIOFile file(filename, "rb", key, ctr, 0);
	
	if (!file.IsOpen()) {
		return false;
	}

	NCCH_Header ncch_header;
	
	if (file.ReadBytes(&ncch_header, sizeof(NCCH_Header)) != sizeof(NCCH_Header)) {
		return false;
	}

	return isHeaderReadable(ncch_header);
}

static void toLower(std::string &str)
{
	for(size_t i=0; i<str.length(); i++)
	{
		str[i] = (char)std::tolower(str[i]);
	}
}

static void saveDigest(std::string digest)
{
	// ADD digest at the end of the file
	LOG_ERROR(HW, "saveDigest");
	
    const std::string path{
        fmt::format("{}/digests.txt", FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir))};
		
    if (!FileUtil::CreateFullPath(path)) {
        LOG_ERROR(Service_FS, "Failed to create digests.txt");
        return;
    }
	
    FileUtil::IOFile file{path, "a"};
    if (!file.IsOpen()) {
        LOG_ERROR(Service_FS, "Failed to open digests.txt");
        return;
    }
	
	file.WriteBytes("\n", 1);
	
	if (file.WriteBytes(digest.c_str(), digest.length()) != digest.length()) {
        LOG_ERROR(Service_FS, "Failed to write digest fully");
    }
	
	file.WriteBytes("\n", 1);
}

static void loadDigests(std::map<std::string, int> &digests)
{
    const std::string filepath = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + "digests.txt";
    FileUtil::CreateFullPath(filepath);

    boost::iostreams::stream<boost::iostreams::file_descriptor_source> file;
    FileUtil::OpenFStream<std::ios_base::in>(file, filepath);
	
    if (file.is_open())
	{
		while (!file.eof())
		{
			std::string line;
			std::getline(file, line);
			
			if(line.ends_with("\r"))
			{
				line.pop_back();
			}
			
			toLower(line);

			if (line.length() == 64 && !line.starts_with("#"))
			{
				digests[line] = 1;
			}
		}
	}
	
	LoadOTP();
	
	if (ct_cert.IsValid() && otp.Valid() && otp.GetDeviceID() != 0x34333231) {	// ignore dummy otp
		struct {
			ECC::PublicKey pkey;
			u32 device_id;
			u32 id;
		} hash_data;
		hash_data.pkey = ct_cert.GetPublicKeyECC();
		hash_data.device_id = otp.GetDeviceID();
		hash_data.id = static_cast<u32>(UniqueCryptoFileID::NCCH);

		u8 digest[CryptoPP::SHA256::DIGESTSIZE];
		CryptoPP::SHA256 hash;
		hash.CalculateDigest(digest, reinterpret_cast<CryptoPP::byte*>(&hash_data), sizeof(hash_data));

		std::string sdigest = binToHex(digest);

		if(digests[sdigest] == 0)
		{
			saveDigest(sdigest);
		}
    }
}

static std::string findDigest(std::string filename)
{
	std::string ret;
	std::map<std::string, int> digests;
	
	loadDigests(digests);
	
	for(auto it=digests.begin(); it!=digests.end(); it++)
	{
		if(testDigest(it->first, filename))
		{
			ret = it->first;
		}
	}
	
	return ret;
}

bool IsUniqueCryptoFile(FileUtil::IOFileBase* file, UniqueCryptoFileID id) {
	std::string sdigest = findDigest(file->Filename());
	
	return sdigest.length() == 64;
}

std::unique_ptr<FileUtil::IOFileBase> OpenUniqueCryptoFile(
	std::unique_ptr<FileUtil::IOFileBase>&& underlying_file, const char openmode[],
    UniqueCryptoFileID id) {
	std::string sdigest = findDigest(underlying_file->Filename());

	if(sdigest.length() == 64)
	{
		u8 digest[CryptoPP::SHA256::DIGESTSIZE];
		memcpy(digest, hexToBin(sdigest).data(), 32);
		
		std::vector<u8> key(0x10);
		std::vector<u8> ctr(0x10);
		memcpy(key.data(), digest, 0x10);
		memcpy(ctr.data(), digest + 0x10, 12);

//		LOG_ERROR(HW, "digest dump {}", binToHex(digest));

		return std::make_unique<FileUtil::CryptoIOFile>(std::move(underlying_file), openmode, key, ctr);
	}
	
	return std::make_unique<FileUtil::NullIOFile>();
}

std::unique_ptr<FileUtil::IOFileBase> OpenUniqueCryptoFile(const std::string& filename,
                                                           const char openmode[],
                                                           UniqueCryptoFileID id, int flags) {
	std::string sdigest = findDigest(filename);

	if(sdigest.length() == 64)
	{
		u8 digest[CryptoPP::SHA256::DIGESTSIZE];
		memcpy(digest, hexToBin(sdigest).data(), 32);
		
		std::vector<u8> key(0x10);
		std::vector<u8> ctr(0x10);
		memcpy(key.data(), digest, 0x10);
		memcpy(ctr.data(), digest + 0x10, 12);

//		LOG_ERROR(HW, "digest dump {}", binToHex(digest));

		return std::make_unique<FileUtil::CryptoIOFile>(filename, openmode, key, ctr, flags);
	}
	
	return std::make_unique<FileUtil::IOFile>(filename, openmode);
}

bool IsFullConsoleLinked() {
    return GetOTP().Valid() && GetSecureInfoA().IsValid() && GetLocalFriendCodeSeedB().IsValid();
}

void UnlinkConsole() {
/*    // Remove all console unique data, as well as the act, nim and frd savefiles
    const std::string system_save_data_path =
        FileSys::GetSystemSaveDataContainerPath(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir));
    constexpr std::array<std::array<u8, 8>, 3> save_data_ids{{
        {0x00, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x01, 0x00},
        {0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x01, 0x00},
        {0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x01, 0x00},
    }};

    for (auto& id : save_data_ids) {
        const std::string final_path = FileSys::GetSystemSaveDataPath(system_save_data_path, id);
        FileUtil::DeleteDirRecursively(final_path, 2);
    }

    FileUtil::Delete(GetOTPPath());
    FileUtil::Delete(GetSecureInfoAPath());
    FileUtil::Delete(GetLocalFriendCodeSeedBPath());

    InvalidateSecureData();*/
}


static bool isAppEncrypted(const std::string& path)
{
    FileUtil::IOFile file(path, "rb");
	
	if (!file.IsOpen()) {
		return false;
	}

	NCCH_Header ncch_header;
	
	if (file.ReadBytes(&ncch_header, sizeof(NCCH_Header)) != sizeof(NCCH_Header)) {
		return false;
	}
	
	return !isHeaderReadable(ncch_header);
}

std::vector<std::string> GetAppFilepaths()
{
	std::vector<std::string> ret;
	
    FileUtil::FSTEntry data_dir;
    std::vector<FileUtil::FSTEntry> files;
    FileUtil::ScanDirectoryTree(FileUtil::GetUserPath(FileUtil::UserPath::UserDir), data_dir, 2048);
    FileUtil::GetAllFilesFromNestedEntries(data_dir, files);
	
	for(size_t i=0; i<files.size(); i++)
	{
		std::string file = files[i].physicalName;
		
		if(file.ends_with(".app")
		&& isAppEncrypted(file))
		{
			ret.push_back(file);
		}
	}
	
	return ret;
}

int RevertEncryptionRemoval()
{
	int res = 0;
	
    FileUtil::FSTEntry data_dir;
    std::vector<FileUtil::FSTEntry> files;
    FileUtil::ScanDirectoryTree(FileUtil::GetUserPath(FileUtil::UserPath::UserDir), data_dir, 2048);
    FileUtil::GetAllFilesFromNestedEntries(data_dir, files);
	
	for(size_t i=0; i<files.size(); i++)
	{
		std::string file = files[i].physicalName;
		
		if(file.ends_with(".app.encrypted"))
		{
			std::string shortName = file.substr(0, file.length() - std::string(".encrypted").length());
			
			if(FileUtil::Exists(shortName))
			{
				std::string sdigest = findDigest(file);
				
				if(sdigest.length() == 64)
				{
					FileUtil::Rename(shortName, shortName + ".decrypted");
					FileUtil::Rename(file, shortName);
					res ++;
				}
			}
		}
		else if(file.ends_with(".app.decrypted"))
		{
			std::string shortName = file.substr(0, file.length() - std::string(".decrypted").length());
			
			if(!FileUtil::Exists(shortName))
			{
				FileUtil::Rename(file, shortName);
			}
		}
	}
	
	return res;
}

int RemoveAzaharEncryption(const std::string& path)
{
	int ret = 0;
	LOG_ERROR(HW, "RemoveAzaharEncryption {}", path);
	
	if(FileUtil::Exists(path + ".decrypted"))
	{
		FileUtil::Rename(path, path + ".encrypted");
		FileUtil::Rename(path + ".decrypted", path);
		
		return 0;
	}
	
	std::string sdigest = findDigest(path);
	
	if(sdigest.length() == 64)
	{
		u8 digest[CryptoPP::SHA256::DIGESTSIZE];
		memcpy(digest, hexToBin(sdigest).data(), 32);
		
		std::vector<u8> key(0x10);
		std::vector<u8> ctr(0x10);
		memcpy(key.data(), digest, 0x10);
		memcpy(ctr.data(), digest + 0x10, 12);

//		LOG_ERROR(HW, "digest dump {}", binToHex(digest));

		FileUtil::CryptoIOFile cfile(path, "rb", key, ctr, 0);
		FileUtil::Delete(path + ".decrypting");
		FileUtil::IOFile dfile(path + ".decrypting", "wb");
		char* buffer = new char[1000000];
		int tocopy = (int)cfile.ReadBytes(buffer, 1000000);
		int written = 0;
		
		while(tocopy > 0)
		{
			written = (int)dfile.WriteBytes(buffer, tocopy);
			
			if(written != tocopy)
			{
				ret = 1;
				LOG_ERROR(HW, "copy error {}", path);
				break;
			}
			
			tocopy = (int)cfile.ReadBytes(buffer, 1000000);
		}
		
		cfile.Close();
		dfile.Close();
		delete[] buffer;
		
		if(ret == 0)
		{
			FileUtil::Rename(path + ".decrypting", path + ".decrypted");
			FileUtil::Rename(path, path + ".encrypted");
			FileUtil::Rename(path + ".decrypted", path);
		}
	}
	else
	{
		ret = 2;
		LOG_ERROR(HW, "no digest found {}", path);
	}
	
	return ret;
}

} // namespace HW::UniqueData
