// V8Unpack.cpp : Defines the entry point for the console application.
//

#include "V8File.h"
#include "version.h"
#include <iostream>
#include <algorithm>

using namespace std;

void usage()
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
}

void version()
{
	cout << V8P_VERSION << endl;
}

int main(int argc, char* argv[])
{

	string cur_mode;
	if (argc > 1)
		cur_mode = argv[1];

	transform(cur_mode.begin(), cur_mode.end(), cur_mode.begin(), ::tolower);

	int ret = 0;

	if (cur_mode == "-version" || cur_mode == "-v") {

		version();
		return 0;
	}


	if (cur_mode == "-inflate" || cur_mode == "-i" || cur_mode == "-und" || cur_mode == "-undeflate") {

		CV8File V8File;
		V8File.Inflate(std::string(argv[2]), std::string(argv[3]));
		return ret;
	}

	if (cur_mode == "-deflate" || cur_mode == "-d") {

		CV8File V8File;
		ret = V8File.Deflate(std::string(argv[2]), std::string(argv[3]));
		return ret;
	}


	if (cur_mode == "-unpack" || cur_mode == "-u" || cur_mode == "-unp") {

		CV8File V8File;
		ret = V8File.UnpackToFolder(std::string(argv[2]), std::string(argv[3]), argv[4], true);
		return ret;
	}


	if (cur_mode == "-pack" || cur_mode == "-pa") {

		CV8File V8File;
		ret = V8File.PackFromFolder(argv[2], argv[3]);
		return ret;
	}

	if (cur_mode == "-parse" || cur_mode == "-p") {

		CV8File V8File;
		ret = V8File.Parse(argv[2], argv[3]);
		return ret;
	}

	if (cur_mode == "-build" || cur_mode == "-b") {

		bool dont_pack = false;
		int argbase = 2;

		if (argc > 2) {
			string arg2(argv[2]);
			transform(arg2.begin(), arg2.end(), arg2.begin(), ::tolower);
			if (arg2 == "-n" || arg2 == "-nopack") {
				argbase++;
				dont_pack = true;
			}
		}

		CV8File V8File;

		ret = V8File.BuildCfFile(argv[argbase], argv[argbase + 1], dont_pack);
		if (ret == SHOW_USAGE)
			usage();

		return ret;
	}


	if (cur_mode == "-bat") {

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

		return ret;
	}

	if (cur_mode == "-example" || cur_mode == "-e") {

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

		return ret;
	}

	usage();
	return 1;
}
