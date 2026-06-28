#include "core/zip_pass.h"
#include "common/logging/log.h"
#include "common/file_util.h"
#include "common/common_paths.h"
#include "core/hle/service/cecd/cecd.h"
#include <zip.h>
#include "core/hle/kernel/shared_page.h"
#include <cryptopp/osrng.h>
#include "core/system_titles.h"

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
				real_name = AndroidUtils::TranslateFilePath(real_name);
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

static int zipPassChecks()
{
	int nHomes = 0;
	
	for (u32 region = 0; region < Core::NUM_SYSTEM_TITLE_REGIONS; region++) {
		if(region == 3) continue;
		const auto hpath = Core::GetHomeMenuNcchPath(region);
	
		if(!hpath.empty() && FileUtil::Exists(hpath))
		{
			nHomes++;
		}
	}

	if(nHomes < 1) {
		LOG_ERROR(Frontend, "importZipPass impossible without system files");
		return -2;
	}
	
	LOG_ERROR(Frontend, "nHomes {}", nHomes);
	
	if(!Settings::values.enable_required_online_lle_modules.GetValue()) {
		LOG_ERROR(Frontend, "importZipPass impossible without LLE modules");
		return -3;
	}
	
	return 0;
}

int importZipPass(std::string path)
{
	LOG_ERROR(Frontend, "importZipPass {}", path);
	
	int ret = zipPassChecks();
	int err = 0;
	
	if(ret) return ret;
	
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
		
		std::string boxInfoPath = inboxPath + DIR_SEP + "BoxInfo_____";
		
		if (!FileUtil::Exists(boxInfoPath))
		{
			LOG_ERROR(HW, "no boxInfo {}", boxInfoPath);
			continue;
		}
		
		struct Service::CECD::Module::CecBoxInfoHeader boxInfo;
		FileUtil::IOFile bfile(boxInfoPath, "rb+");
		int nRead = bfile.ReadBytes(&boxInfo, sizeof(Service::CECD::Module::CecBoxInfoHeader));
		
		if(st.size > boxInfo.max_message_size)
		{
			bfile.Close();
			LOG_ERROR(HW, "message too big {} / {}", st.size, boxInfo.max_message_size);
			continue;
		}
		
		const std::string ext_inbox_path{fmt::format("{}/zippass/inboxes/{}/", 
					FileUtil::GetUserPath(FileUtil::UserPath::UserDir),
					id)};
		bool ext_inbox = false;

		if(boxInfo.message_num >= boxInfo.max_message_num)
		{
			FileUtil::CreateFullPath(ext_inbox_path);
			ext_inbox = true;
			LOG_ERROR(HW, "streetpass inbox full {} / {} -> external inbox", boxInfo.message_num, boxInfo.max_message_num);
			
			FileUtil::FSTEntry data_dir;
			std::vector<FileUtil::FSTEntry> files;
			FileUtil::ScanDirectoryTree(ext_inbox_path, data_dir, 2048);
			FileUtil::GetAllFilesFromNestedEntries(data_dir, files);
			
			if (files.size() > 99) {
				bfile.Close();
				LOG_ERROR(Service_FS, "external inbox is full");
				continue;
			}
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
		
		CryptoPP::AutoSeededRandomPool rng;
		rng.GenerateBlock(messHead->message_id.data(), messHead->message_id.size());
		filename = "_" + Service::CECD::Module::EncodeBase64(messHead->message_id);
		
		std::string path = inboxPath + DIR_SEP + filename;
		
		if(ext_inbox) {
			path = ext_inbox_path + filename;
		}
		
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
		
		if(!ext_inbox) {
			boxInfo.message_num++;
			boxInfo.box_info_size += 0x70;
			boxInfo.box_size += st.size;
			
			bfile.Seek(0, SEEK_SET);
			bfile.WriteBytes(&boxInfo, sizeof(Service::CECD::Module::CecBoxInfoHeader));
			
			bfile.Seek(0, SEEK_END);
			bfile.WriteBytes(buff, 0x70);
		}
		
		bfile.Close();
		delete[] buff;
		
		ret++;
	}
	
	err = zip_close(za);
	LOG_ERROR(HW, "zip_close {}", err);
	
	return ret;
}

int importQueuedZipPass()
{
	LOG_ERROR(HW, "importQueuedZipPass");
	
	int chck = zipPassChecks();
	if(chck) return chck;
	
	FileUtil::FSTEntry data_dir;
    std::vector<FileUtil::FSTEntry> files;
	const std::string inboxes_path{fmt::format("{}/zippass/inboxes", FileUtil::GetUserPath(FileUtil::UserPath::UserDir))};
	const std::string queue_path{fmt::format("{}/zippass/queue", FileUtil::GetUserPath(FileUtil::UserPath::UserDir))};
	const std::string history_path{fmt::format("{}/zippass/history/", FileUtil::GetUserPath(FileUtil::UserPath::UserDir))};
	
	if (!FileUtil::CreateFullPath(history_path)) {
		LOG_ERROR(Service_FS, "Failed to create history_path");
		return -10;
	}
	
    FileUtil::ScanDirectoryTree(inboxes_path, data_dir, 2048);
    FileUtil::GetAllFilesFromNestedEntries(data_dir, files);
	
	for(size_t i=0; i<files.size(); i++)
	{
		std::string file = files[i].physicalName;
		auto filepath_elems = FileUtil::SplitPathComponents(file);
		
		if(filepath_elems.size() > 1) {
			std::string filename = filepath_elems.back();
			filepath_elems.pop_back();
			std::string folder = filepath_elems.back();
			
			LOG_ERROR(Service_FS, "Import from ext inbox {} / {}", folder, filename);
			
			std::string inbox = FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)
			+ DIR_SEP + "data" + DIR_SEP + "00000000000000000000000000000000" 
			+ DIR_SEP + "sysdata" + DIR_SEP + "00010026" + DIR_SEP + "00000000" 
			+ DIR_SEP + "CEC" + DIR_SEP + folder + DIR_SEP + "InBox___";
			
			std::string boxInfoPath = inbox + DIR_SEP + "BoxInfo_____";
			
			if (FileUtil::IsDirectory(inbox) && FileUtil::Exists(boxInfoPath))
			{
				struct Service::CECD::Module::CecBoxInfoHeader boxInfo;
				FileUtil::IOFile bfile(boxInfoPath, "rb+");
				int nRead = bfile.ReadBytes(&boxInfo, sizeof(Service::CECD::Module::CecBoxInfoHeader));
				
				if(boxInfo.message_num >= boxInfo.max_message_num)
				{
					LOG_ERROR(Service_FS, "streetpass inbox full {} / {}", boxInfo.message_num, boxInfo.max_message_num);
					bfile.Close();
					continue;
				}
				
				unsigned char* buff = new unsigned char[0x70];
				FileUtil::IOFile spfile(file, "rb");
				
				spfile.ReadBytes(buff, 0x70);
				spfile.Close();
				
				u64 size = FileUtil::GetSize(file);
				FileUtil::Rename(file, inbox + DIR_SEP + filename);
				
				boxInfo.message_num++;
				boxInfo.box_info_size += 0x70;
				boxInfo.box_size += size;
				
				bfile.Seek(0, SEEK_SET);
				bfile.WriteBytes(&boxInfo, sizeof(Service::CECD::Module::CecBoxInfoHeader));
				
				bfile.Seek(0, SEEK_END);
				bfile.WriteBytes(buff, 0x70);
				
				delete[] buff;
				bfile.Close();
			}
		}
		
		FileUtil::Delete(file);
	}
	
	data_dir.children.clear();
	files.clear();
    FileUtil::ScanDirectoryTree(queue_path, data_dir, 2048);
    FileUtil::GetAllFilesFromNestedEntries(data_dir, files);
	
	for(size_t i=0; i<files.size(); i++)
	{
		std::string file = files[i].physicalName;
		
		if(file.ends_with(".pass.zip"))
		{
			std::string zip_path = file;
			
#ifdef ANDROID
			zip_path = AndroidUtils::TranslateFilePath(file);
#endif

			int ret = Core::importZipPass(zip_path);
			
			if(ret < 0) {
				return ret;
			}
			
			const std::string newPath = history_path + FileUtil::SplitPathComponents(file).back();
			
			FileUtil::Delete(newPath);
			FileUtil::Rename(file, newPath);
		}
		
		FileUtil::Delete(file);
	}
	
	Core::trimZipPassHistory();
	
	return 0;
}

void trimZipPassHistory()
{
	const std::string history_path{fmt::format("{}/zippass/history/", FileUtil::GetUserPath(FileUtil::UserPath::UserDir))};
	FileUtil::FSTEntry data_dir;
    std::vector<FileUtil::FSTEntry> files;
	
    FileUtil::ScanDirectoryTree(history_path, data_dir, 2048);
    FileUtil::GetAllFilesFromNestedEntries(data_dir, files);
	
	int toRemove = files.size() - 100;
	
	if(toRemove > 0) {
		std::map<time_t, std::string> historyFiles;
		
		for(auto file : files) {
			historyFiles[FileUtil::GetDate(file.physicalName)] = file.physicalName;
		}
		
		for(auto it = historyFiles.begin(); it != historyFiles.end() && toRemove > 0; it++) {
			FileUtil::Delete(it->second);
			toRemove--;
		}
	}
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
