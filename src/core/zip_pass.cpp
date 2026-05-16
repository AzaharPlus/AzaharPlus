#include "core/zip_pass.h"
#include "common/logging/log.h"
#include "common/file_util.h"
#include "common/common_paths.h"
#include "core/hle/service/cecd/cecd.h"
#include <zip.h>
#include "core/hle/kernel/shared_page.h"

namespace Core {

static int addFileToZip(zip_t *za, const char* zipPath, const char* srcPath)
{
	int ret = -2;
	zip_source_t *src = zip_source_file_create(srcPath, 0, ZIP_LENGTH_TO_END, NULL);
	
	if(src == NULL) return -1;
	
	int err = zip_file_add(za, zipPath, src, 0);
	
	if(err == -1) zip_source_free(src);
	else ret = 0;
	
	return ret;
}

int exportZipPass(std::string path)
{
	int ret = 0;
	
	LOG_ERROR(Frontend, "exportZipPass {}", path);
	
	int err = 0;
	zip_t *za = zip_open(path.c_str(), ZIP_CREATE|ZIP_TRUNCATE, &err);
	LOG_ERROR(HW, "zip_open {}", err);
	
	if(err != 0) return -1;
	
    const auto callback = [za](u64* num_entries_out, const std::string& directory,
                                          const std::string& virtual_name) -> bool {
        const std::string physical_name = directory + DIR_SEP + virtual_name;
        if (FileUtil::IsDirectory(physical_name) && virtual_name.length() == 8) {
            LOG_ERROR(Frontend, "streetpass directory {}", physical_name);
			
			const auto callback2 = [za, virtual_name](u64* num_entries_out, const std::string& directory,
												  const std::string& v_name) -> bool {
				std::string real_name = directory + DIR_SEP + v_name;
#ifdef ANDROID
				real_name = AndroidStorage::TranslateFilePath(real_name);
#endif
				if (v_name[0] == '_' && v_name.length() == 12) {
					LOG_ERROR(Frontend, "streetpass file {}", FileUtil::SanitizePath(real_name));
					addFileToZip(za, (virtual_name + "/" + v_name).c_str(), FileUtil::SanitizePath(real_name).c_str());
				}
				return true;
			};
			
			FileUtil::ForeachDirectoryEntry(nullptr, physical_name + DIR_SEP + "OutBox__", callback2);
        }
        return true;
    };
	
	const std::string dir = FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)
		+ DIR_SEP + "data" + DIR_SEP + "00000000000000000000000000000000" 
		+ DIR_SEP + "sysdata" + DIR_SEP + "00010026" + DIR_SEP + "00000000" 
		+ DIR_SEP + "CEC";

    FileUtil::ForeachDirectoryEntry(nullptr, dir, callback);
	
	ret = zip_get_num_entries(za, 0);
	
	err = zip_close(za);
	LOG_ERROR(HW, "zip_close {}", err);
	
	if(err < 0) ret = -1;
	
	return ret;
}

int importZipPass(std::string path)
{
	LOG_ERROR(Frontend, "importZipPass {}", path);
	
	int ret = 0;
	int err = 0;
	zip_t *za = zip_open(path.c_str(), ZIP_RDONLY, &err);
	LOG_ERROR(HW, "zip_open {}", err);
	
	if(err != 0) ret = -1;
	
	int num = zip_get_num_entries(za, 0);
	LOG_ERROR(HW, "zip_get_num_entries {}", num);
	
	for(int i=0; i<num; i++)
	{
		struct zip_stat st;
		err = zip_stat_index(za, i, 0, &st);
		LOG_ERROR(HW, "zip_stat_index {}", err);
		
		if(st.valid & ZIP_STAT_NAME)
		{
			LOG_ERROR(HW, "zip_stat_index name {}", st.name);
		} else continue;
		
		if(st.valid & ZIP_STAT_SIZE)
		{
			LOG_ERROR(HW, "zip_stat_index size {}", st.size);
		} else continue;
		
		if(st.size < 0x70)
		{
			LOG_ERROR(HW, "size too small {}", st.size);
			continue;
		}
		
		std::vector<std::string> elems = FileUtil::SplitPathComponents(st.name);
		
		if(elems.size() != 2)
		{
			LOG_ERROR(HW, "bad dir structure {}", st.name);
			continue;
		}
		
		std::string id = elems[0];
		std::string filename = elems[1];
		
		if(filename[0] != '_' || filename.length() != 12)
		{
			LOG_ERROR(HW, "bad filename {}", filename);
			continue;
		}
		
		std::string inboxPath = FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)
			+ DIR_SEP + "data" + DIR_SEP + "00000000000000000000000000000000" 
			+ DIR_SEP + "sysdata" + DIR_SEP + "00010026" + DIR_SEP + "00000000" 
			+ DIR_SEP + "CEC" + DIR_SEP + id + DIR_SEP + "InBox___";
		
		if (!FileUtil::IsDirectory(inboxPath))
		{
			LOG_ERROR(HW, "no inbox {}", inboxPath);
			continue;
		}
		
		std::string path = inboxPath + DIR_SEP + filename;
		std::string boxInfoPath = inboxPath + DIR_SEP + "BoxInfo_____";
		
		if (!FileUtil::Exists(boxInfoPath))
		{
			LOG_ERROR(HW, "no boxInfo {}", boxInfoPath);
			continue;
		}
		
		struct Service::CECD::Module::CecBoxInfoHeader boxInfo;
		FileUtil::IOFile bfile(boxInfoPath, "rb+");
		int nRead = bfile.ReadBytes(&boxInfo, sizeof(Service::CECD::Module::CecBoxInfoHeader));
		
		if(boxInfo.message_num >= boxInfo.max_message_num
			|| st.size > boxInfo.max_message_size)
		{
			bfile.Close();
			LOG_ERROR(HW, "box full {} / {} or message too big {} / {}", 
				boxInfo.message_num, boxInfo.max_message_num, st.size, boxInfo.max_message_size);
			continue;
		}		
		
		zip_file_t *file = zip_fopen_index(za, i, 0);
		
		unsigned char* buff = new unsigned char[st.size];
		
		int n = zip_fread(file, buff, st.size);
		LOG_ERROR(HW, "zip_fread n {}", n);
		zip_fclose(file);
		
		Service::CECD::Module::CecMessageHeader* messHead = (Service::CECD::Module::CecMessageHeader*)buff;
		
		std::string b64_messageId = "_" + Service::CECD::Module::EncodeBase64(messHead->message_id);
		
		std::string sTitleId = "";
		unsigned char* bTitleId = (unsigned char*)&messHead->title_id;
	
		for(int i=3; i>=0; i--)
		{
			std::string s = fmt::format("{:02x}", bTitleId[i]);
			sTitleId += s;
		}
	
		if(	messHead->magic != 0x6060
			|| messHead->message_size != st.size
			|| sTitleId != id
			|| b64_messageId != filename)
		{
			LOG_ERROR(HW, "bad message header {} {} {} {}", 
				messHead->magic, messHead->message_size, sTitleId, b64_messageId);
			
			bfile.Close();
			delete[] buff;
			
			continue;
		}
		
		auto initTime = SharedPage::GetInitTime(0);
		std::chrono::system_clock::time_point tp(initTime);
		std::time_t time = std::chrono::system_clock::to_time_t(tp);
		std::tm tm = *std::localtime(&time);
		
		LOG_ERROR(HW, "timestamp {} / {} / {} - {} : {} : {}",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
		
		messHead->send_time.year=tm.tm_year + 1900;
        messHead->send_time.month=tm.tm_mon + 1;
        messHead->send_time.day=tm.tm_mday;
        messHead->send_time.hour=tm.tm_hour;
        messHead->send_time.minute=tm.tm_min;
        messHead->send_time.second=tm.tm_sec;
        messHead->send_time.millisecond=1;
        messHead->send_time.microsecond=1;
        messHead->send_time.padding=1;
		
		messHead->recv_time = messHead->send_time;
		
		FileUtil::IOFile dfile(path, "wb");
	
		int written = (int)dfile.WriteBytes(buff, st.size);
		LOG_ERROR(HW, "WriteBytes n {}", written);

		dfile.Close();
		
		if(written != st.size)
		{
			LOG_ERROR(HW, "written != st.size {} / {}", written, st.size);
			
			bfile.Close();
			delete[] buff;
			
			ret = -1;
			break;
		}
		
		boxInfo.message_num++;
		boxInfo.box_info_size += 0x70;
		boxInfo.box_size += st.size;
		
		bfile.Seek(0, SEEK_SET);
		bfile.WriteBytes(&boxInfo, sizeof(Service::CECD::Module::CecBoxInfoHeader));
		
		bfile.Seek(0, SEEK_END);
		bfile.WriteBytes(buff, 0x70);
		
		bfile.Close();
		delete[] buff;
		
		ret++;
	}
	
	err = zip_close(za);
	LOG_ERROR(HW, "zip_close {}", err);
	
	return ret;
}

int clearStreetPassConfig()
{
	const auto callback = [](u64* num_entries_out, const std::string& directory,
                                          const std::string& virtual_name) -> bool {
        const std::string physical_name = directory + DIR_SEP + virtual_name;
        if (FileUtil::IsDirectory(physical_name)) {
            LOG_ERROR(Frontend, "streetpass directory to delete {}", physical_name);
			FileUtil::DeleteDirRecursively(physical_name);
        }
        return true;
    };
	
	const std::string dir = FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)
		+ DIR_SEP + "data" + DIR_SEP + "00000000000000000000000000000000" 
		+ DIR_SEP + "sysdata" + DIR_SEP + "00010026" + DIR_SEP + "00000000" 
		+ DIR_SEP + "CEC";

    FileUtil::ForeachDirectoryEntry(nullptr, dir, callback);

	return 0;
}

} // namespace Core
