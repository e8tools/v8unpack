/*----------------------------------------------------------
This Source Code Form is subject to the terms of the 
Mozilla Public License, v.2.0. If a copy of the MPL 
was not distributed with this file, You can obtain one 
at http://mozilla.org/MPL/2.0/.
----------------------------------------------------------*/
/////////////////////////////////////////////////////////////////////////////
//	Author:			disa_da
//	E-mail:			disa_da2@mail.ru
/////////////////////////////////////////////////////////////////////////////

/**
    2014-2022       dmpas       sergey(dot)batanov(at)dmpas(dot)ru
    2019-2020       fishca      fishcaroot(at)gmail(dot)com
 */

// main.cpp : Defines the entry point for the console application.
//

#include "V8File.h"
#include "VersionsTable.h"
#include "version.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <utility>
#include <boost/filesystem/fstream.hpp>

using namespace std;
using namespace v8unpack;

typedef int (*handler_t)(vector<string> &argv);
void read_param_file(const char *filename, vector< vector<string> > &list);
handler_t get_run_mode(const vector<string> &args, int &arg_base, bool &allow_listfile);

int usage(vector<string> &argv)
{
	cout << endl;
	cout << "V8Upack Version " << V8P_VERSION
		 << " Copyright (c) " << V8P_RIGHT << endl;

	cout << endl;
	cout << "Unpack, pack, deflate and inflate 1C v8 file (*.cf)" << endl;
	cout << endl;
	cout << "V8UNPACK" << endl;
	cout << "  -U[NPACK]            in_filename.cf     out_dirname [block_name]" << endl;
	cout << "  -U[NPACK]            in_filename.cf     -             block_name" << endl;
	cout << "  -U[NPACK]  -L[IST]   listfile" << endl;
	cout << "  -PA[CK]              in_dirname         out_filename.cf" << endl;
	cout << "  -PA[CK]    -L[IST]   listfile" << endl;
	cout << "  -I[NFLATE]           in_filename.data   out_filename" << endl;
	cout << "  -I[NFLATE] -L[IST]   listfile" << endl;
	cout << "  -D[EFLATE]           in_filename        filename.data" << endl;
	cout << "  -D[EFLATE] -L[IST]   listfile" << endl;
	cout << "  -P[ARSE]             in_filename        out_dirname [block_name1 block_name2 ...]" << endl;
	cout << "  -P[ARSE]   -L[IST]   listfile" << endl;
	cout << "  -DEL[ETE]            in_filename        [block_mask1 block_mask2 ...]" << endl;
	cout << "  -DEL[ETE]  -L[IST]   listfile" << endl;
	cout << "  -ADD [-PACK|-BUILD [-NOPACK]] [-N[AME] name] source|- out_filename" << endl;
	cout << "  -ADD [-PACK|-BUILD [-NOPACK]] -LISTFILES|-LF listfile out_filename" << endl;
	cout << "  -PUT [-PACK|-BUILD [-NOPACK]] [-N[AME] name] source|- out_filename" << endl;
	cout << "  -PUT [-PACK|-BUILD [-NOPACK]] -LISTFILES|-LF listfile out_filename" << endl;
	cout << "  -B[UILD] [-N[OPACK]] in_dirname         out_filename" << endl;
	cout << "  -B[UILD] [-N[OPACK]] -L[IST] listfile" << endl;
	cout << "  -L[IST]              listfile" << endl;
	
	cout << "  -LISTFILES|-LF       in_filename" << endl;
	cout << "  -VERSIONSFILE|-VF    -SHOW   in_filename" << endl;
	cout << "  -VERSIONSFILE|-VF    -GET    in_filename  block_name" << endl;
	cout << "  -VERSIONSFILE|-VF    -SET    in_filename  block_name  version" << endl;
	cout << "  -VERSIONSFILE|-VF    -SET    -LIST|-LF listfile  in_filename" << endl;
	cout << "  -VERSIONSFILE|-VF    -UPDATE in_filename  block_name" << endl;
	cout << "  -VERSIONSFILE|-VF    -UPDATE -LIST|-LF listfile  in_filename" << endl;

	cout << "  -E[XAMPLE]" << endl;
	cout << "  -BAT" << endl;
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
	if (argv[1] == "-" && argv[2].empty()) {
		return V8UNPACK_SHOW_USAGE;
	}
	int ret = UnpackToFolder(argv[0], argv[1], argv[2], true);
	return ret;
}

int pack(vector<string> &argv)
{
	int ret = PackFromFolder(argv[0], argv[1]);
	return ret;
}

int parse(vector<string> &argv)
{

	if (argv.size() < 2) {
		return V8UNPACK_SHOW_USAGE;
	}

	vector<string> filter;
	for (size_t i = 2; i < argv.size(); i++) {
		if (!argv[i].empty()) {
			filter.push_back(argv[i]);
		}
	}

	return Parse(argv[0], argv[1], filter);
}

int list_files(vector<string> &argv)
{
	int ret = ListFiles(argv[0]);
	return ret;
}

int delete_blocks(vector<string> &argv)
{
	if (argv.empty() || argv[0].empty()) {
		return V8UNPACK_SHOW_USAGE;
	}

	vector<string> masks;
	for (size_t i = 1; i < argv.size(); i++) {
		if (!argv[i].empty()) {
			masks.push_back(argv[i]);
		}
	}

	return DeleteBlocks(argv[0], masks);
}

static string to_lower_copy(string value)
{
	transform(value.begin(), value.end(), value.begin(), ::tolower);
	return value;
}

int add_or_put(vector<string> &argv, bool replace)
{
	AddMode mode = AddMode::Pack;
	string elem_name;
	string listfile;
	vector<string> positional;

	for (size_t i = 0; i < argv.size(); i++) {
		if (argv[i].empty()) {
			continue;
		}

		string al = to_lower_copy(argv[i]);
		if (al == "-") {
			positional.push_back(argv[i]);
			continue;
		}
		if (al[0] != '-') {
			positional.push_back(argv[i]);
			continue;
		}
		if (al == "-pack" || al == "-pa") {
			mode = AddMode::Pack;
			continue;
		}
		if (al == "-build" || al == "-b") {
			mode = AddMode::Build;
			continue;
		}
		if (al == "-nopack") {
			mode = AddMode::BuildNopack;
			continue;
		}
		if (al == "-name" || al == "-n") {
			if (i + 1 >= argv.size() || argv[i + 1].empty()) {
				return V8UNPACK_SHOW_USAGE;
			}
			elem_name = argv[++i];
			continue;
		}
		if (al == "-listfiles" || al == "-lf") {
			if (i + 1 >= argv.size() || argv[i + 1].empty()) {
				return V8UNPACK_SHOW_USAGE;
			}
			listfile = argv[++i];
			continue;
		}
		return V8UNPACK_SHOW_USAGE;
	}

	if (!listfile.empty()) {
		if (positional.empty() || positional[0].empty()) {
			return V8UNPACK_SHOW_USAGE;
		}

		boost::filesystem::ifstream in(listfile);
		if (!in) {
			cerr << "Add. List file not found: " << listfile << endl;
			return V8UNPACK_SOURCE_DOES_NOT_EXIST;
		}

		vector<AddItem> items;
		string line;
		while (getline(in, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (line.empty()) {
				continue;
			}
			AddItem item;
			item.source = line;
			items.push_back(item);
		}

		if (items.empty()) {
			return V8UNPACK_SHOW_USAGE;
		}

		return AddToContainer(positional[0], items, mode, replace);
	}

	if (positional.size() < 2 || positional[0].empty() || positional[1].empty()) {
		return V8UNPACK_SHOW_USAGE;
	}

	if (positional[0] == "-" && elem_name.empty()) {
		return V8UNPACK_SHOW_USAGE;
	}

	AddItem item;
	item.source = positional[0];
	item.name = elem_name;
	return AddToContainer(positional[1], {item}, mode, replace);
}

int add(vector<string> &argv)
{
	return add_or_put(argv, false);
}

int put(vector<string> &argv)
{
	return add_or_put(argv, true);
}

static bool is_vf_list_flag(const string &value)
{
	return value == "-lf" || value == "-listfiles" || value == "-list" || value == "-l";
}

static string trim_copy(string value)
{
	size_t begin = 0;
	while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t')) {
		begin++;
	}
	size_t end = value.size();
	while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r')) {
		end--;
	}
	return value.substr(begin, end - begin);
}

static bool split_name_version(const string &line, string &name, string &version)
{
	size_t i = 0;
	while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
		i++;
	}
	if (i == 0 || i >= line.size()) {
		return false;
	}
	name = line.substr(0, i);
	version = trim_copy(line.substr(i));
	return !version.empty();
}

static int read_set_listfile(const string &listfile, vector<pair<string, string>> &items)
{
	boost::filesystem::ifstream in(listfile);
	if (!in) {
		cerr << "VersionsFile. List file not found: " << listfile << endl;
		return V8UNPACK_SOURCE_DOES_NOT_EXIST;
	}

	string line;
	while (getline(in, line)) {
		line = trim_copy(line);
		if (line.empty()) {
			continue;
		}
		string name;
		string version;
		if (!split_name_version(line, name, version)) {
			cerr << "VersionsFile. Invalid list line: " << line << endl;
			return V8UNPACK_ERROR;
		}
		items.push_back(make_pair(name, version));
	}

	if (items.empty()) {
		return V8UNPACK_SHOW_USAGE;
	}
	return V8UNPACK_OK;
}

static int read_update_listfile(const string &listfile, vector<string> &names)
{
	boost::filesystem::ifstream in(listfile);
	if (!in) {
		cerr << "VersionsFile. List file not found: " << listfile << endl;
		return V8UNPACK_SOURCE_DOES_NOT_EXIST;
	}

	string line;
	while (getline(in, line)) {
		line = trim_copy(line);
		if (line.empty()) {
			continue;
		}
		names.push_back(line);
	}

	if (names.empty()) {
		return V8UNPACK_SHOW_USAGE;
	}
	return V8UNPACK_OK;
}

int versionsfile(vector<string> &argv)
{
	if (argv.empty() || argv[0].empty()) {
		return V8UNPACK_SHOW_USAGE;
	}

	string cmd = to_lower_copy(argv[0]);
	if (cmd == "-show") {
		if (argv.size() < 2 || argv[1].empty()) {
			return V8UNPACK_SHOW_USAGE;
		}
		return VersionsShow(argv[1]);
	}
	if (cmd == "-get") {
		if (argv.size() < 3 || argv[1].empty()) {
			return V8UNPACK_SHOW_USAGE;
		}
		return VersionsGet(argv[1], argv[2]);
	}

	if (cmd == "-set" || cmd == "-update") {
		string listfile;
		vector<string> positional;

		for (size_t i = 1; i < argv.size(); i++) {
			if (argv[i].empty()) {
				continue;
			}
			string al = to_lower_copy(argv[i]);
			if (is_vf_list_flag(al)) {
				if (i + 1 >= argv.size() || argv[i + 1].empty()) {
					return V8UNPACK_SHOW_USAGE;
				}
				listfile = argv[++i];
				continue;
			}
			if (al[0] == '-') {
				return V8UNPACK_SHOW_USAGE;
			}
			positional.push_back(argv[i]);
		}

		if (!listfile.empty()) {
			if (positional.size() != 1 || positional[0].empty()) {
				return V8UNPACK_SHOW_USAGE;
			}
			if (cmd == "-set") {
				vector<pair<string, string>> items;
				int ret = read_set_listfile(listfile, items);
				if (ret != V8UNPACK_OK) {
					return ret;
				}
				return VersionsSet(positional[0], items);
			}
			vector<string> names;
			int ret = read_update_listfile(listfile, names);
			if (ret != V8UNPACK_OK) {
				return ret;
			}
			return VersionsUpdate(positional[0], names);
		}

		if (cmd == "-set") {
			if (positional.size() < 3 || positional[0].empty() || positional[2].empty()) {
				return V8UNPACK_SHOW_USAGE;
			}
			return VersionsSet(positional[0], positional[1], positional[2]);
		}
		if (positional.size() < 2 || positional[0].empty()) {
			return V8UNPACK_SHOW_USAGE;
		}
		return VersionsUpdate(positional[0], positional[1]);
	}

	return V8UNPACK_SHOW_USAGE;
}

int process_list(vector<string> &argv)
{
	if (argv.empty()) {
		return V8UNPACK_SHOW_USAGE;
	}

	vector< vector<string> > commands;
	read_param_file(argv.at(0).c_str(), commands);

	for (auto command : commands) {

		int arg_base = 0;
		bool allow_listfile = false;

		handler_t handler = get_run_mode(command, arg_base, allow_listfile);

		command.erase(command.begin());
		int ret = handler(command);
		if (ret != 0) {
			// выходим по первой ошибке
			return ret;
		}
	}

	return 0;
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
	int ret = BuildCfFile(argv[0], argv[1], false);
	return ret;
}

int build_nopack(vector<string> &argv)
{
	int ret = BuildCfFile(argv[0], argv[1], true);
	return ret;
}

handler_t get_run_mode(const vector<string> &args, int &arg_base, bool &allow_listfile)
{
	if (args.size() - arg_base < 1) {
		allow_listfile = false;
		return usage;
	}

	allow_listfile = true;
	string cur_mode(args[arg_base]);
	transform(cur_mode.begin(), cur_mode.end(), cur_mode.begin(), ::tolower);

	arg_base += 1;
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

	if (cur_mode == "-delete" || cur_mode == "-del") {
		return delete_blocks;
	}

	if (cur_mode == "-add") {
		allow_listfile = false;
		return add;
	}

	if (cur_mode == "-put") {
		allow_listfile = false;
		return put;
	}

	if (cur_mode == "-versionsfile" || cur_mode == "-vf") {
		allow_listfile = false;
		return versionsfile;
	}

	if (cur_mode == "-build" || cur_mode == "-b") {

		bool dont_pack = false;

		while ((int)args.size() > arg_base) {
			string arg2(args[arg_base]);
			transform(arg2.begin(), arg2.end(), arg2.begin(), ::tolower);
			if (arg2 == "-n" || arg2 == "-nopack") {
				arg_base++;
				dont_pack = true;
			} else {
				break;
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

	if (cur_mode == "-list" || cur_mode == "-l") {
		return process_list;
	}

	if (cur_mode == "-listfiles" || cur_mode == "-lf") {
		return list_files;
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

		while (current_line.size() < 5) {
			// Дополним пустыми строками, чтобы избежать лишних проверок
			current_line.emplace_back("");
		}

		list.push_back(current_line);
	}
}

int main(int argc, char* argv[])
{
	int arg_base = 1;
	bool allow_listfile = false;
	vector<string> args;
	for (int i = 0; i < argc; i++) {
		args.emplace_back(argv[i]);
	}
	handler_t handler = get_run_mode(args, arg_base, allow_listfile);

	vector<string> cli_args;

	if (handler == nullptr) {
		usage(cli_args);
		return 1;
	}

	if (allow_listfile && arg_base < argc) {
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
		cli_args.emplace_back(string(argv[i]));
	}
	while (cli_args.size() < 3) {
		// Дополним пустыми строками, чтобы избежать лишних проверок
		cli_args.emplace_back("");
	}

	int ret = handler(cli_args);
	if (ret == V8UNPACK_SHOW_USAGE) {
		usage(cli_args);
	}
	return ret;
}
