#include "headers_scanner.h"
#include <peconv.h>

HeadersScanReport* HeadersScanner::scanRemote()
{
	if (!moduleData.isInitialized() && !moduleData.loadOriginal()) {
		std::cerr << "[-] Module not initialized" << std::endl;
		return nullptr;
	}

	HeadersScanReport *my_report = new HeadersScanReport(this->processHandle, moduleData.moduleHandle, remoteModData.getModuleSize());
	if (!remoteModData.isInitialized()) {
		std::cerr << "[-] Failed to read the module header" << std::endl;
		my_report->status = SCAN_ERROR;
		return my_report;
	}

	BYTE hdr_buffer1[peconv::MAX_HEADER_SIZE] = { 0 };
	memcpy(hdr_buffer1, remoteModData.headerBuffer, peconv::MAX_HEADER_SIZE);
	my_report->is64 = peconv::is64bit(hdr_buffer1);
	my_report->isDotNetModule = moduleData.isDotNet();

	size_t hdrs_size = peconv::get_hdrs_size(hdr_buffer1);
	if (hdrs_size > peconv::MAX_HEADER_SIZE) {
		hdrs_size = peconv::MAX_HEADER_SIZE;
	}

	BYTE hdr_buffer2[peconv::MAX_HEADER_SIZE] = { 0 };
	memcpy(hdr_buffer2, moduleData.original_module, hdrs_size);

	// some .NET modules overwrite their own headers, so at this point they should be excluded from the comparison
	DWORD ep1 = peconv::get_entry_point_rva(hdr_buffer1);
	DWORD ep2 = peconv::get_entry_point_rva(hdr_buffer2);
	if (ep1 != ep2) {
		my_report->epModified = true;
	}
	DWORD arch1 = peconv::get_nt_hdr_architecture(hdr_buffer1);
	DWORD arch2 = peconv::get_nt_hdr_architecture(hdr_buffer2);
	if (arch1 != arch2) {
		my_report->archMismatch = true;
		//if there is an architecture mismatch it may indicate that a different version of the app was loaded (possibly legit)
		//TODO: implement a better verification, for now mark as suspicious
		my_report->status = SCAN_SUSPICIOUS;
		return my_report;
	}
	//normalize before comparing:
	peconv::update_image_base(hdr_buffer1, 0);
	peconv::update_image_base(hdr_buffer2, 0);

	zeroUnusedFields(hdr_buffer1, hdrs_size);
	zeroUnusedFields(hdr_buffer2, hdrs_size);

	//compare:
	if (memcmp(hdr_buffer1, hdr_buffer2, hdrs_size) == 0) {
		my_report->status = SCAN_NOT_SUSPICIOUS;
		return my_report;
	}
	//modifications detected, now find more details:
	my_report->dosHdrModified = isDosHdrModified(hdr_buffer1, hdr_buffer2, hdrs_size);
	my_report->fileHdrModified = isFileHdrModified(hdr_buffer1, hdr_buffer2, hdrs_size);
	my_report->ntHdrModified = isNtHdrModified(hdr_buffer1, hdr_buffer2, hdrs_size);
	my_report->secHdrModified = isSecHdrModified(hdr_buffer1, hdr_buffer2, hdrs_size);

	my_report->status = SCAN_SUSPICIOUS;
	return my_report;
}

bool HeadersScanner::zeroUnusedFields(PBYTE hdr_buffer, size_t hdrs_size)
{
	size_t section_num = peconv::get_sections_count(hdr_buffer, hdrs_size);
	bool is_modified = false;

	for (size_t i = 0; i < section_num; i++) {
		PIMAGE_SECTION_HEADER sec_hdr = peconv::get_section_hdr(hdr_buffer, hdrs_size, i);
		if (sec_hdr == nullptr) continue;
		if (sec_hdr->SizeOfRawData == 0) {
			sec_hdr->PointerToRawData = 0;
			is_modified = true;
		}
	}
	return is_modified;
}

bool HeadersScanner::isDosHdrModified(const PBYTE hdr_buffer1, const PBYTE hdr_buffer2, const size_t hdrs_size)
{
	if (hdrs_size < sizeof(IMAGE_DOS_HEADER)) { //should never happen
		return false;
	}
	IMAGE_DOS_HEADER* hdr1 = (IMAGE_DOS_HEADER*)hdr_buffer1;
	IMAGE_DOS_HEADER* hdr2 = (IMAGE_DOS_HEADER*)hdr_buffer2;
	if (memcmp(hdr1, hdr2, sizeof(IMAGE_DOS_HEADER)) != 0) {
		return true;
	}

	LONG new_hdr = hdr2->e_lfanew;
	if (memcmp(hdr1, hdr2, new_hdr) != 0) {
		return true;
	}
	return false;
}

bool HeadersScanner::isSecHdrModified(const PBYTE hdr_buffer1, const PBYTE hdr_buffer2, const size_t hdrs_size)
{
	size_t section_num1 = peconv::get_sections_count(hdr_buffer1, hdrs_size);
	size_t section_num2 = peconv::get_sections_count(hdr_buffer2, hdrs_size);
	if (section_num1 != section_num2) {
		return true;
	}

	for (size_t i = 0; i < section_num1; i++) {
		PIMAGE_SECTION_HEADER sec_hdr1 = peconv::get_section_hdr(hdr_buffer1, hdrs_size, i);
		PIMAGE_SECTION_HEADER sec_hdr2 = peconv::get_section_hdr(hdr_buffer2, hdrs_size, i);
		if (!sec_hdr1 && !sec_hdr2) {
			continue;
		}
		else if (!sec_hdr1 || !sec_hdr2) {
			return true; //modified
		}

		if (sec_hdr1->VirtualAddress != sec_hdr2->VirtualAddress) {
			return true;
		}
		if (sec_hdr1->Misc.VirtualSize != sec_hdr2->Misc.VirtualSize) {
			return true;
		}
		if (sec_hdr1->PointerToRawData != sec_hdr2->PointerToRawData) {
			return true;
		}
	}
	return false;
}

bool HeadersScanner::isFileHdrModified(const PBYTE hdr_buffer1, const PBYTE hdr_buffer2, const size_t hdrs_size)
{
	const IMAGE_FILE_HEADER *file_hdr1 = peconv::get_file_hdr(hdr_buffer1, hdrs_size);
	const IMAGE_FILE_HEADER *file_hdr2 = peconv::get_file_hdr(hdr_buffer2, hdrs_size);

	if (!file_hdr1 && !file_hdr2) return false;
	if (!file_hdr1 || !file_hdr2) return true;

	if (memcmp(file_hdr1, file_hdr2, sizeof(IMAGE_NT_HEADERS64)) == 0) {
		return false;
	}
	return true;
}

bool HeadersScanner::isNtHdrModified(const PBYTE hdr_buffer1, const PBYTE hdr_buffer2, const size_t hdrs_size)
{
	bool is64 = peconv::is64bit(hdr_buffer1);
	if (peconv::is64bit(hdr_buffer2) != is64) {
		return true;
	}
	BYTE *nt1 = peconv::get_nt_hdrs(hdr_buffer1, hdrs_size);
	BYTE *nt2 = peconv::get_nt_hdrs(hdr_buffer2, hdrs_size);
	if (!nt1 && !nt2) return false;
	if (!nt1 || !nt2) return true;

	if (is64) {
		IMAGE_NT_HEADERS64* nt1_64 = (IMAGE_NT_HEADERS64*) nt1;
		IMAGE_NT_HEADERS64* nt2_64 = (IMAGE_NT_HEADERS64*) nt2;
		if (memcmp(nt1, nt2, sizeof(IMAGE_NT_HEADERS64)) == 0) {
			return false;
		}
	}
	else {
		IMAGE_NT_HEADERS64* nt1_64 = (IMAGE_NT_HEADERS64*)nt1;
		IMAGE_NT_HEADERS64* nt2_64 = (IMAGE_NT_HEADERS64*)nt2;
		if (memcmp(nt1, nt2, sizeof(IMAGE_NT_HEADERS64)) == 0) {
			return false;
		}
	}
	return true;
}
