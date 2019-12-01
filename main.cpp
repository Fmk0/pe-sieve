// Scans for modified modules within the process of a given PID
// author: hasherezade (hasherezade@gmail.com)

#include <Windows.h>
#include <Psapi.h>
#include <sstream>
#include <fstream>

#include "utils/process_privilege.h"

#include "utils/util.h"

#include "pe_sieve.h"
#include "params_info/pe_sieve_params_info.h"

#include "color_scheme.h"

#define PARAM_SWITCH1 '/'
#define PARAM_SWITCH2 '-'
//scan options:
#define PARAM_PID "pid"
#define PARAM_SHELLCODE "shellc"
#define PARAM_DATA "data"
#define PARAM_MODULES_FILTER "mfilter"
//dump options:
#define PARAM_IMP_REC "imp"
#define PARAM_DUMP_MODE "dmode"
//output options:
#define PARAM_OUT_FILTER "ofilter"
#define PARAM_QUIET "quiet"
#define PARAM_JSON "json"
#define PARAM_DIR "dir"
#define PARAM_MINIDUMP "minidmp"
//info:
#define PARAM_HELP "help"
#define PARAM_HELP2  "?"
#define PARAM_VERSION "version"

using namespace pesieve;

void print_in_color(int color, const std::string &text)
{
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	FlushConsoleInputBuffer(hConsole);
	SetConsoleTextAttribute(hConsole, color); // back to default color
	std::cout << text;
	FlushConsoleInputBuffer(hConsole);
	SetConsoleTextAttribute(hConsole, 7); // back to default color
}

void print_param_in_color(int color, const std::string &text)
{
	print_in_color(color, PARAM_SWITCH1 + text);
}


void print_help()
{
	const int hdr_color = HEADER_COLOR;
	const int param_color = HILIGHTED_COLOR;
	const int separator_color = SEPARATOR_COLOR;
	print_in_color(hdr_color, "Required: \n");
	print_param_in_color(param_color, PARAM_PID);
	std::cout << " <target_pid>\n\t: Set the PID of the target process.\n";
	print_in_color(hdr_color, "\nOptional: \n");

	print_in_color(separator_color, "\n---scan options---\n");

	print_param_in_color(param_color, PARAM_SHELLCODE);
	std::cout << "\t: Detect shellcode implants. (By default it detects PE only).\n";
	print_param_in_color(param_color, PARAM_DATA);
	std::cout << "\t: If DEP is disabled scan also non-executable memory\n\t(which potentially can be executed).\n";
#ifdef _WIN64
	print_param_in_color(param_color, PARAM_MODULES_FILTER);
	std::cout << " <*mfilter_id>\n\t: Filter the scanned modules.\n";
	std::cout << "*mfilter_id:\n";
	for (DWORD i = 0; i <= LIST_MODULES_ALL; i++) {
		std::cout << "\t" << i << " - " << translate_modules_filter(i) << "\n";
	}
#endif

	print_in_color(separator_color, "\n---dump options---\n");
	print_param_in_color(param_color, PARAM_IMP_REC);
	std::cout << " <*imprec_mode>\n\t: Set in which mode the ImportTable should be recovered.\n";;
	std::cout << "*imprec_mode:\n";
	for (size_t i = 0; i < PE_IMPREC_MODES_COUNT; i++) {
		t_imprec_mode mode = (t_imprec_mode)(i);
		std::cout << "\t" << mode << " - " << translate_imprec_mode(mode) << "\n";
	}

	print_param_in_color(param_color, PARAM_DUMP_MODE);
	std::cout << " <*dump_mode>\n\t: Set in which mode the detected PE files should be dumped.\n";
	std::cout << "*dump_mode:\n";
	for (DWORD i = 0; i < peconv::PE_DUMP_MODES_COUNT; i++) {
		peconv::t_pe_dump_mode mode = (peconv::t_pe_dump_mode)(i);
		std::cout << "\t" << mode << " - " << translate_dump_mode(mode) << "\n";
	}

	print_in_color(separator_color, "\n---output options---\n");

	print_param_in_color(param_color, PARAM_OUT_FILTER);
	std::cout << " <*ofilter_id>\n\t: Filter the dumped output.\n";
	std::cout << "*ofilter_id:\n";
	for (size_t i = 0; i < OUT_FILTERS_COUNT; i++) {
		t_output_filter mode = (t_output_filter)(i);
		std::cout << "\t" << mode << " - " << translate_out_filter(mode) << "\n";
	}

	print_param_in_color(param_color, PARAM_QUIET);
	std::cout << "\t: Print only the summary. Do not log on stdout during the scan.\n";
	print_param_in_color(param_color, PARAM_JSON);
	std::cout << "\t: Print the JSON report as the summary.\n";

	print_param_in_color(param_color, PARAM_MINIDUMP);
	std::cout << ": Create a minidump of the full suspicious process.\n";
	
	print_param_in_color(param_color, PARAM_DIR);
	std::cout << " <output_dir>\n\t: Set a root directory for the output (default: current directory).\n";
	print_in_color(hdr_color, "\nInfo: \n");
	print_param_in_color(param_color, PARAM_HELP);
	std::cout << "    : Print this help.\n";
	print_param_in_color(param_color, PARAM_VERSION);
	std::cout << " : Print version number.\n";
	std::cout << "---" << std::endl;
}

void banner()
{
	char logo[] = "\
.______    _______           _______. __   ___________    ____  _______ \n\
|   _  \\  |   ____|         /       ||  | |   ____\\   \\  /   / |   ____|\n\
|  |_)  | |  |__    ______ |   (----`|  | |  |__   \\   \\/   /  |  |__   \n\
|   ___/  |   __|  |______| \\   \\    |  | |   __|   \\      /   |   __|  \n\
|  |      |  |____      .----)   |   |  | |  |____   \\    /    |  |____ \n\
| _|      |_______|     |_______/    |__| |_______|   \\__/     |_______|\n";

	char logo2[] = "\
  _        _______       _______      __   _______     __       _______ \n";
	char logo3[] = "\
________________________________________________________________________\n";
	print_in_color(2, logo);
	print_in_color(4, logo2);
	print_in_color(4, logo3);
	std::cout << "\n";
	std::cout << info();
	std::cout <<  "---\n";
	print_help();
}

void print_report(const ProcessScanReport& report, const t_params args)
{
	std::string report_str;
	if (args.json_output) {
		report_str = report_to_json(report, REPORT_SUSPICIOUS_AND_ERRORS);
	} else {
		report_str = report_to_string(report);
	}
	//summary:
	const t_report summary = report.generateSummary();
	std::cout << report_str;
	if (!args.json_output) {
		std::cout << "---" << std::endl;
	}
}

bool set_output_dir(t_params &args, const char *new_dir)
{
	if (!new_dir) return false;

	size_t new_len = strlen(new_dir);
	size_t buffer_len = sizeof(args.output_dir);
	if (new_len > buffer_len) return false;

	memset(args.output_dir, 0, buffer_len);
	memcpy(args.output_dir, new_dir, new_len);
	return true;
}

void print_unknown_param(const char *param)
{
	print_in_color(WARNING_COLOR, "Invalid parameter: ");
	std::cout << param << "\n";
}

bool is_param(const char *str)
{
	if (!str) return false;

	const size_t len = strlen(str);
	if (len < 2) return false;

	if (str[0] == PARAM_SWITCH1 || str[0] == PARAM_SWITCH2) {
		return true;
	}
	return false;
}

bool is_num(const char *str)
{
	if (!str) return false;

	const size_t len = strlen(str);
	if (len < 1) return false;

	for (size_t i = 0; i < len; i++) {
		if (!isdigit(str[i])) return false;
	}
	return true;
}

long get_number(const char* str)
{
	const char hex_pattern[] = "0x";
	size_t hex_pattern_len = strlen(hex_pattern);

	const size_t len = strlen(str);
	if (len == 0) return 0;

	size_t cmp_len = len;
	if (len > hex_pattern_len) cmp_len = hex_pattern_len;

	long out = 0;

	try {
		if (strncmp(str, hex_pattern, cmp_len) == 0) {
			out = std::stoul(str, nullptr, 16);
		}
		else {
			out = std::stoul(str, nullptr, 10);
		}
	}
	catch (const std::exception & e) {
		print_in_color(WARNING_COLOR, "Invalid parameter: ");
		std::cout << str << "\n";
	}
	return out;
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		banner();
		system("pause");
		return 0;
	}
	//---
	bool info_req = false;
	t_params args = { 0 };
	args.modules_filter = LIST_MODULES_ALL;

	//Parse parameters
	for (int i = 1; i < argc; i++) {
		if (!is_param(argv[i])) {
			if (i == 1 && is_num(argv[i])) {
				//allow for PID as a first parameter
				continue;
			}
			// if the argument didn't have a param switch, print info but do not exit
			print_unknown_param(argv[i]);
			continue;
		}
		const char *param = &argv[i][1];
		if (!strcmp(param, PARAM_HELP) || !strcmp(param, PARAM_HELP2)) {
			print_help();
			info_req = true;
		}
		else if (!strcmp(param, PARAM_IMP_REC)) {
			args.imprec_mode = PE_IMPREC_AUTO;
			if ((i + 1) < argc) {
				char* mode_num = argv[i + 1];
				if (isdigit(mode_num[0])) {
					args.imprec_mode = normalize_imprec_mode(atoi(mode_num));
					++i;
				}
			}
		}
		else if (!strcmp(param, PARAM_OUT_FILTER) && (i + 1) < argc) {
			args.out_filter = static_cast<t_output_filter>(atoi(argv[i + 1]));
			i++;
		} 
		else if (!strcmp(param, PARAM_MODULES_FILTER) && (i + 1) < argc) {
			args.modules_filter = atoi(argv[i + 1]);
			if (args.modules_filter > LIST_MODULES_ALL) {
				args.modules_filter = LIST_MODULES_ALL;
			}
			i++;
		}
		else if (!strcmp(param, PARAM_PID) && (i + 1) < argc) {
			args.pid = get_number(argv[i + 1]);
			++i;
		}
		else if (!strcmp(param, PARAM_VERSION)) {
			std::cout << PESIEVE_VERSION << "\n";
			info_req = true;
		}
		else if (!strcmp(param, PARAM_QUIET)) {
			args.quiet = true;
		}
		else if (!strcmp(param, PARAM_JSON)) {
			args.json_output = true;
		}
		else if (!strcmp(param, PARAM_MINIDUMP)) {
			args.minidump = true;
		}
		else if (!strcmp(param, PARAM_SHELLCODE)) {
			args.shellcode = true;
		}
		else if (!strcmp(param, PARAM_DATA)) {
			args.data = true;
		}
		else if (!strcmp(param, PARAM_DUMP_MODE) && (i + 1) < argc) {
			args.dump_mode = normalize_dump_mode(atoi(argv[i + 1]));
			++i;
		} else if (!strcmp(param, PARAM_DIR) && (i + 1) < argc) {
			set_output_dir(args, argv[i + 1]);
			++i;
		} else {
			print_unknown_param(argv[i]);
			print_in_color(HILIGHTED_COLOR, "Available parameters:\n\n");
			print_help();
			return 0;
		}
	}
	//if didn't received PID by explicit parameter, try to parse the first param of the app
	if (args.pid == 0) {
		if (info_req) {
#ifdef _DEBUG
			system("pause");
#endif
			return 0; // info requested, pid not given. finish.
		}
		if (argc >= 2 && is_num(argv[1])) args.pid = atoi(argv[1]);
		if (args.pid == 0) {
			print_help();
			return 0;
		}
	}
	//---
	if (!args.quiet) {
		std::cout << "PID: " << args.pid << std::endl;
		std::cout << "Modules filter: " << translate_modules_filter(args.modules_filter) << std::endl;
		std::cout << "Output filter: " << translate_out_filter(args.out_filter) << std::endl;
		std::cout << "Dump mode: " << translate_dump_mode(args.dump_mode) << std::endl;
	}
	ProcessScanReport* report = scan_process(args);
	if (report != nullptr) {
		print_report(*report, args);
		delete report;
		report = nullptr;
	}
#ifdef _DEBUG
	system("pause");
#endif
	return 0;
}
