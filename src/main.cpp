/////////////////////////////////////////////////////////////////////////////
//
//
//	Author:			disa_da
//	E-mail:			disa_da2@mail.ru
//
//
/////////////////////////////////////////////////////////////////////////////

/**
    2014-2017       dmpas       sergey(dot)batanov(at)dmpas(dot)ru
 */

// main.cpp : Defines the entry point for the console application.
//

#include "V8File.h"
#include "version.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

using namespace std;

int usage(vector<string> &argv)
{
	cout << endl;
	cout << "V8Upack Version " << V8P_VERSION
		 << " Copyright (c) " << V8P_RIGHT << endl;

	cout << endl;
	cout << "Unpack, pack, deflate and inflate 1C v8 file (*.cf)" << endl;
	cout << endl;
	cout << "V8UNPACK" << endl;
	cout << "  -U[NPACK]            in_filename.cf     out_dirname" << endl;
	cout << "  -PA[CK]              in_dirname         out_filename.cf" << endl;
	cout << "  -I[NFLATE]           in_filename.data   out_filename" << endl;
	cout << "  -D[EFLATE]           in_filename        filename.data" << endl;
	cout << "  -E[XAMPLE]" << endl;
	cout << "  -BAT" << endl;
	cout << "  -P[ARSE]             in_filename        out_dirname" << endl;
	cout << "  -B[UILD] [-N[OPACK]] in_dirname         out_filename" << endl;
	cout << "  -V[ERSION]" << endl;

	return 0;
}

int version(vector<string> &argv)
{
	cout << V8P_VERSION << endl;
	return 0;
}

int inflate(vector<string> &argv)
{
	int ret = Inflate(argv[0], argv[1]);
	return ret;
}

int deflate(vector<string> &argv)
{
	int ret = Deflate(argv[0], argv[1]);
	return ret;
}

int unpack(vector<string> &argv)
{
	int ret = CV8File::UnpackToFolder(argv[0], argv[1], argv[2], true);
	return ret;
}

int pack(vector<string> &argv)
{
	CV8File V8File;
	int ret = V8File.PackFromFolder(argv[0], argv[1]);
	return ret;
}

int parse(vector<string> &argv)
{
	int ret = CV8File::Parse(argv[0], argv[1]);
	return ret;
}

int bat(vector<string> &argv)
{
	cout << "if %1 == P GOTO PACK" << endl;
	cout << "if %1 == p GOTO PACK" << endl;
	cout << "" << endl;
	cout << "" << endl;
	cout << ":UNPACK" << endl;
	cout << "V8Unpack.exe -unpack      %2                              %2.unp" << endl;
	cout << "V8Unpack.exe -undeflate   %2.unp\\metadata.data            %2.unp\\metadata.data.und" << endl;
	cout << "V8Unpack.exe -unpack      %2.unp\\metadata.data.und        %2.unp\\metadata.unp" << endl;
	cout << "GOTO END" << endl;
	cout << "" << endl;
	cout << "" << endl;
	cout << ":PACK" << endl;
	cout << "V8Unpack.exe -pack        %2.unp\\metadata.unp            %2.unp\\metadata_new.data.und" << endl;
	cout << "V8Unpack.exe -deflate     %2.unp\\metadata_new.data.und   %2.unp\\metadata.data" << endl;
	cout << "V8Unpack.exe -pack        %2.unp                         %2.new.cf" << endl;
	cout << "" << endl;
	cout << "" << endl;
	cout << ":END" << endl;

	return 0;
}

int example(vector<string> &argv)
{
	cout << "" << endl;
	cout << "" << endl;
	cout << "UNPACK" << endl;
	cout << "V8Unpack.exe -unpack      1Cv8.cf                         1Cv8.unp" << endl;
	cout << "V8Unpack.exe -undeflate   1Cv8.unp\\metadata.data          1Cv8.unp\\metadata.data.und" << endl;
	cout << "V8Unpack.exe -unpack      1Cv8.unp\\metadata.data.und      1Cv8.unp\\metadata.unp" << endl;
	cout << "" << endl;
	cout << "" << endl;
	cout << "PACK" << endl;
	cout << "V8Unpack.exe -pack        1Cv8.unp\\metadata.unp           1Cv8.unp\\metadata_new.data.und" << endl;
	cout << "V8Unpack.exe -deflate     1Cv8.unp\\metadata_new.data.und  1Cv8.unp\\metadata.data" << endl;
	cout << "V8Unpack.exe -pack        1Cv8.und                        1Cv8_new.cf" << endl;
	cout << "" << endl;
	cout << "" << endl;

	return 0;
}

int build(vector<string> &argv)
{
	const bool dont_pack = false;
	int ret = CV8File::BuildCfFile(argv[0], argv[1], dont_pack);
	return ret;
}

int build_nopack(vector<string> &argv)
{
	const bool dont_pack = true;
	int ret = CV8File::BuildCfFile(argv[0], argv[1], dont_pack);
	return ret;
}

typedef int (*handler_t)(vector<string> &argv);

handler_t getRunMode(char *argv[], int argc, int &arg_base, bool &allow_listfile)
{
	if (argc < 2) {
		allow_listfile = false;
		return usage;
	}

	allow_listfile = true;
	string cur_mode(argv[1]);
	transform(cur_mode.begin(), cur_mode.end(), cur_mode.begin(), ::tolower);

	arg_base = 2;
	if (cur_mode == "-version" || cur_mode == "-v") {
		allow_listfile = false;
		return version;
	}

	if (cur_mode == "-inflate" || cur_mode == "-i" || cur_mode == "-und" || cur_mode == "-undeflate") {
		return inflate;
	}

	if (cur_mode == "-deflate" || cur_mode == "-d") {
		return deflate;
	}

	if (cur_mode == "-unpack" || cur_mode == "-u" || cur_mode == "-unp") {
		return unpack;
	}

	if (cur_mode == "-pack" || cur_mode == "-pa") {
		return pack;
	}

	if (cur_mode == "-parse" || cur_mode == "-p") {
		return parse;
	}

	if (cur_mode == "-build" || cur_mode == "-b") {

		bool dont_pack = false;

		if (argc > arg_base) {
			string arg2(argv[arg_base]);
			transform(arg2.begin(), arg2.end(), arg2.begin(), ::tolower);
			if (arg2 == "-n" || arg2 == "-nopack") {
				arg_base++;
				dont_pack = true;
			}
		}
		return dont_pack ? build_nopack : build;
	}

	allow_listfile = false;
	if (cur_mode == "-bat") {
		return bat;
	}

	if (cur_mode == "-example" || cur_mode == "-e") {
		return example;
	}

	return nullptr;
}

void read_param_file(const char *filename, vector< vector<string> > &list)
{
	boost::filesystem::ifstream in(filename);
	string line;
	while (getline(in, line)) {

		vector<string> current_line;

		stringstream ss;
		ss.str(line);

		string item;
		while (getline(ss, item, ';')) {
			current_line.push_back(item);
		}

		while (current_line.size() < 3) {
			// Дополним пустыми строками, чтобы избежать лишних проверок
			current_line.push_back("");
		}

		list.push_back(current_line);
	}
}

int main(int argc, char* argv[])
{
	int arg_base = 1;
	bool allow_listfile = false;
	handler_t handler = getRunMode(argv, argc, arg_base, allow_listfile);

	vector<string> cli_args;

	if (handler == nullptr) {
		usage(cli_args);
		return 1;
	}

	if (allow_listfile && arg_base <= argc) {
		string a_list(argv[arg_base]);
		transform(a_list.begin(), a_list.end(), a_list.begin(), ::tolower);
		if (a_list == "-list" || a_list == "-l") {
			// Передан файл с параметрами
			vector< vector<string> > param_list;
			read_param_file(argv[arg_base + 1], param_list);

			int ret = 0;

			for (auto argv_from_file : param_list) {
				int ret1 = handler(argv_from_file);
				if (ret1 != 0 && ret == 0) {
					ret = ret1;
				}
			}

			return ret;
		}
	}

	for (int i = arg_base; i < argc; i++) {
		cli_args.push_back(string(argv[i]));
	}
	while (cli_args.size() < 3) {
		// Дополним пустыми строками, чтобы избежать лишних проверок
		cli_args.push_back("");
	}

	int ret = handler(cli_args);
	if (ret == SHOW_USAGE) {
		usage(cli_args);
	}
	return ret;
}
