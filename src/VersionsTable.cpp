/*----------------------------------------------------------
This Source Code Form is subject to the terms of the
Mozilla Public License, v.2.0. If a copy of the MPL
was not distributed with this file, You can obtain one
at http://mozilla.org/MPL/2.0/.
----------------------------------------------------------*/

#include "VersionsTable.h"
#include "V8File.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <utility>
#include <vector>
#include <boost/filesystem/fstream.hpp>

namespace v8unpack {

using namespace std;

enum class VersionsKind {
	Uuid,
	Base64
};

struct VersionEntry {
	string name;
	string version;
	size_t version_begin = 0;
	size_t version_end = 0;
};

struct VersionsDocument {
	string data;
	VersionsKind kind = VersionsKind::Uuid;
	vector<VersionEntry> entries;
};

static bool is_ws(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static size_t skip_ws(const string &s, size_t i)
{
	while (i < s.size() && is_ws(s[i])) {
		i++;
	}
	return i;
}

static size_t skip_bom(const string &s)
{
	if (s.size() >= 3
		&& static_cast<unsigned char>(s[0]) == 0xef
		&& static_cast<unsigned char>(s[1]) == 0xbb
		&& static_cast<unsigned char>(s[2]) == 0xbf) {
		return 3;
	}
	return 0;
}

static bool match_brace(const string &s, size_t open, size_t &close)
{
	if (open >= s.size() || s[open] != '{') {
		return false;
	}

	int depth = 0;
	bool in_str = false;
	for (size_t i = open; i < s.size(); i++) {
		char c = s[i];
		if (in_str) {
			if (c == '"') {
				if (i + 1 < s.size() && s[i + 1] == '"') {
					i++;
					continue;
				}
				in_str = false;
			}
			continue;
		}
		if (c == '"') {
			in_str = true;
			continue;
		}
		if (c == '{') {
			depth++;
		} else if (c == '}') {
			depth--;
			if (depth == 0) {
				close = i + 1;
				return true;
			}
		}
	}
	return false;
}

static bool read_quoted(const string &s, size_t &i, string &value)
{
	i = skip_ws(s, i);
	if (i >= s.size() || s[i] != '"') {
		return false;
	}
	i++;
	value.clear();
	while (i < s.size()) {
		if (s[i] == '"') {
			if (i + 1 < s.size() && s[i + 1] == '"') {
				value.push_back('"');
				i += 2;
				continue;
			}
			i++;
			return true;
		}
		value.push_back(s[i]);
		i++;
	}
	return false;
}

static bool read_unquoted(const string &s, size_t &i, string &value, size_t &begin, size_t &end)
{
	i = skip_ws(s, i);
	begin = i;
	while (i < s.size() && s[i] != ',' && s[i] != '}') {
		i++;
	}
	end = i;
	while (end > begin && is_ws(s[end - 1])) {
		end--;
	}
	value.assign(s, begin, end - begin);
	return !value.empty();
}

static bool expect_comma(const string &s, size_t &i)
{
	i = skip_ws(s, i);
	if (i >= s.size() || s[i] != ',') {
		return false;
	}
	i++;
	return true;
}

static int b64_value(char c)
{
	if (c >= 'A' && c <= 'Z') {
		return c - 'A';
	}
	if (c >= 'a' && c <= 'z') {
		return c - 'a' + 26;
	}
	if (c >= '0' && c <= '9') {
		return c - '0' + 52;
	}
	if (c == '+') {
		return 62;
	}
	if (c == '/') {
		return 63;
	}
	return -1;
}

static bool decode_base64(const string &in, vector<unsigned char> &out)
{
	out.clear();
	int val = 0;
	int valb = -8;
	bool saw_pad = false;
	for (char c : in) {
		if (c == '=') {
			saw_pad = true;
			continue;
		}
		if (is_ws(c)) {
			continue;
		}
		if (saw_pad) {
			return false;
		}
		int d = b64_value(c);
		if (d < 0) {
			return false;
		}
		val = (val << 6) + d;
		valb += 6;
		if (valb >= 0) {
			out.push_back(static_cast<unsigned char>((val >> valb) & 0xff));
			valb -= 8;
		}
	}
	return true;
}

static string encode_base64(const unsigned char *data, size_t n)
{
	static const char tbl[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	string out;
	out.reserve(((n + 2) / 3) * 4);
	size_t i = 0;
	while (i + 3 <= n) {
		unsigned int v = (static_cast<unsigned int>(data[i]) << 16)
			| (static_cast<unsigned int>(data[i + 1]) << 8)
			| static_cast<unsigned int>(data[i + 2]);
		out.push_back(tbl[(v >> 18) & 63]);
		out.push_back(tbl[(v >> 12) & 63]);
		out.push_back(tbl[(v >> 6) & 63]);
		out.push_back(tbl[v & 63]);
		i += 3;
	}
	if (i < n) {
		unsigned int v = static_cast<unsigned int>(data[i]) << 16;
		if (i + 1 < n) {
			v |= static_cast<unsigned int>(data[i + 1]) << 8;
		}
		out.push_back(tbl[(v >> 18) & 63]);
		out.push_back(tbl[(v >> 12) & 63]);
		if (i + 1 < n) {
			out.push_back(tbl[(v >> 6) & 63]);
		} else {
			out.push_back('=');
		}
		out.push_back('=');
	}
	return out;
}

static bool is_hex(char c)
{
	return (c >= '0' && c <= '9')
		|| (c >= 'a' && c <= 'f')
		|| (c >= 'A' && c <= 'F');
}

static bool is_uuid(const string &value)
{
	static const int groups[] = {8, 4, 4, 4, 12};
	size_t i = 0;
	for (int g = 0; g < 5; g++) {
		if (g > 0) {
			if (i >= value.size() || value[i] != '-') {
				return false;
			}
			i++;
		}
		for (int n = 0; n < groups[g]; n++) {
			if (i >= value.size() || !is_hex(value[i])) {
				return false;
			}
			i++;
		}
	}
	return i == value.size();
}

static bool is_version_b64(const string &value)
{
	if (value.size() != 28) {
		return false;
	}
	vector<unsigned char> decoded;
	if (!decode_base64(value, decoded)) {
		return false;
	}
	return decoded.size() == 20;
}

static bool valid_version(VersionsKind kind, const string &value)
{
	return kind == VersionsKind::Uuid ? is_uuid(value) : is_version_b64(value);
}

static void random_bytes(unsigned char *buf, size_t n)
{
	random_device rd;
	mt19937_64 gen(
		static_cast<mt19937_64::result_type>(rd())
		^ static_cast<mt19937_64::result_type>(
			chrono::high_resolution_clock::now().time_since_epoch().count())
	);
	uniform_int_distribution<int> dist(0, 255);
	for (size_t i = 0; i < n; i++) {
		buf[i] = static_cast<unsigned char>(dist(gen));
	}
}

static string generate_uuid()
{
	unsigned char buf[16];
	random_bytes(buf, 16);
	static const char hex[] = "0123456789abcdef";
	string out;
	out.resize(36);
	size_t pos = 0;
	for (size_t i = 0; i < 16; i++) {
		if (i == 4 || i == 6 || i == 8 || i == 10) {
			out[pos++] = '-';
		}
		out[pos++] = hex[buf[i] >> 4];
		out[pos++] = hex[buf[i] & 0x0f];
	}
	return out;
}

static string generate_b64()
{
	unsigned char buf[20];
	random_bytes(buf, 20);
	return encode_base64(buf, 20);
}

static bool parse_versions_list(const string &s, size_t begin, size_t end, VersionsDocument &doc)
{
	if (end <= begin + 1 || s[begin] != '{' || s[end - 1] != '}') {
		return false;
	}

	size_t i = begin + 1;
	i = skip_ws(s, i);
	if (i >= end || s[i] == '}') {
		return false;
	}

	string first;
	size_t dummy_b = 0, dummy_e = 0;
	if (!read_unquoted(s, i, first, dummy_b, dummy_e)) {
		return false;
	}
	if (!expect_comma(s, i)) {
		return false;
	}

	i = skip_ws(s, i);
	if (i >= end) {
		return false;
	}

	if (s[i] != '"') {
		string count;
		if (!read_unquoted(s, i, count, dummy_b, dummy_e)) {
			return false;
		}
		if (!expect_comma(s, i)) {
			return false;
		}
		doc.kind = VersionsKind::Uuid;
	} else {
		doc.kind = VersionsKind::Base64;
	}

	while (true) {
		i = skip_ws(s, i);
		if (i >= end) {
			return false;
		}
		if (s[i] == '}') {
			break;
		}

		VersionEntry entry;
		if (!read_quoted(s, i, entry.name)) {
			return false;
		}
		if (!expect_comma(s, i)) {
			return false;
		}
		if (!read_unquoted(s, i, entry.version, entry.version_begin, entry.version_end)) {
			return false;
		}
		doc.entries.push_back(entry);

		i = skip_ws(s, i);
		if (i < end && s[i] == ',') {
			i++;
			continue;
		}
		if (i < end && s[i] == '}') {
			break;
		}
		return false;
	}

	return !doc.entries.empty();
}

static int load_document(const string &filename, VersionsDocument &doc)
{
	boost::filesystem::ifstream in(filename, ios_base::in | ios_base::binary);
	if (!in) {
		cerr << "VersionsFile. `" << filename << "`. Input file not found!" << endl;
		return V8UNPACK_SOURCE_DOES_NOT_EXIST;
	}

	in.seekg(0, ios_base::end);
	auto size = in.tellg();
	if (size < 0) {
		cerr << "VersionsFile. `" << filename << "` is not a versions file!" << endl;
		return V8UNPACK_NOT_VERSIONS_FILE;
	}
	in.seekg(0, ios_base::beg);
	doc.data.resize(static_cast<size_t>(size));
	if (size > 0) {
		in.read(&doc.data[0], size);
		if (!in) {
			cerr << "VersionsFile. `" << filename << "` is not a versions file!" << endl;
			return V8UNPACK_NOT_VERSIONS_FILE;
		}
	}

	size_t i = skip_bom(doc.data);
	vector<pair<size_t, size_t>> lists;
	while (true) {
		i = skip_ws(doc.data, i);
		if (i >= doc.data.size()) {
			break;
		}
		if (doc.data[i] == ',') {
			i++;
			continue;
		}
		if (doc.data[i] != '{') {
			cerr << "VersionsFile. `" << filename << "` is not a versions file!" << endl;
			return V8UNPACK_NOT_VERSIONS_FILE;
		}
		size_t close = 0;
		if (!match_brace(doc.data, i, close)) {
			cerr << "VersionsFile. `" << filename << "` is not a versions file!" << endl;
			return V8UNPACK_NOT_VERSIONS_FILE;
		}
		lists.push_back(make_pair(i, close));
		i = close;
	}

	size_t list_begin = 0;
	size_t list_end = 0;
	if (lists.size() == 1) {
		list_begin = lists[0].first;
		list_end = lists[0].second;
	} else if (lists.size() == 3) {
		list_begin = lists[2].first;
		list_end = lists[2].second;
	} else {
		cerr << "VersionsFile. `" << filename << "` is not a versions file!" << endl;
		return V8UNPACK_NOT_VERSIONS_FILE;
	}

	if (!parse_versions_list(doc.data, list_begin, list_end, doc)) {
		cerr << "VersionsFile. `" << filename << "` is not a versions file!" << endl;
		return V8UNPACK_NOT_VERSIONS_FILE;
	}

	if (lists.size() == 3) {
		doc.kind = VersionsKind::Base64;
	}

	return V8UNPACK_OK;
}

static int find_entry(const VersionsDocument &doc, const string &block_name)
{
	for (size_t i = 0; i < doc.entries.size(); i++) {
		if (doc.entries[i].name == block_name) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

static int save_document(const string &filename, const VersionsDocument &doc)
{
	boost::filesystem::ofstream out(filename, ios_base::out | ios_base::binary | ios_base::trunc);
	if (!out) {
		cerr << "VersionsFile. `" << filename << "`. Cannot write file!" << endl;
		return V8UNPACK_ERROR_CREATING_OUTPUT_FILE;
	}
	if (!doc.data.empty()) {
		out.write(doc.data.data(), static_cast<streamsize>(doc.data.size()));
	}
	if (!out) {
		cerr << "VersionsFile. `" << filename << "`. Cannot write file!" << endl;
		return V8UNPACK_ERROR_CREATING_OUTPUT_FILE;
	}
	return V8UNPACK_OK;
}

static void apply_version(VersionsDocument &doc, int index, const string &version)
{
	auto &entry = doc.entries[static_cast<size_t>(index)];
	doc.data.replace(entry.version_begin, entry.version_end - entry.version_begin, version);
	entry.version = version;
	entry.version_end = entry.version_begin + version.size();
}

struct PendingVersion {
	int index;
	string version;
};

static void apply_versions(VersionsDocument &doc, vector<PendingVersion> &pending)
{
	sort(pending.begin(), pending.end(),
		[&doc](const PendingVersion &a, const PendingVersion &b) {
			return doc.entries[static_cast<size_t>(a.index)].version_begin
				> doc.entries[static_cast<size_t>(b.index)].version_begin;
		});
	for (const auto &item : pending) {
		apply_version(doc, item.index, item.version);
	}
}

int VersionsShow(const string &filename)
{
	VersionsDocument doc;
	int ret = load_document(filename, doc);
	if (ret != V8UNPACK_OK) {
		return ret;
	}

	size_t width = 0;
	for (const auto &entry : doc.entries) {
		if (entry.name.size() > width) {
			width = entry.name.size();
		}
	}

	for (const auto &entry : doc.entries) {
		cout << entry.name;
		size_t pad = width > entry.name.size() ? width - entry.name.size() + 2 : 2;
		cout << string(pad, ' ') << entry.version << '\n';
	}
	cout << flush;
	return V8UNPACK_OK;
}

int VersionsGet(const string &filename, const string &block_name)
{
	VersionsDocument doc;
	int ret = load_document(filename, doc);
	if (ret != V8UNPACK_OK) {
		return ret;
	}

	int index = find_entry(doc, block_name);
	if (index < 0) {
		cerr << "VersionsFile. `" << block_name << "` not found!" << endl;
		return V8UNPACK_ELEM_NOT_FOUND;
	}

	cout << doc.entries[static_cast<size_t>(index)].version << endl;
	return V8UNPACK_OK;
}

int VersionsSet(const string &filename, const vector<pair<string, string>> &items)
{
	if (items.empty()) {
		return V8UNPACK_SHOW_USAGE;
	}

	VersionsDocument doc;
	int ret = load_document(filename, doc);
	if (ret != V8UNPACK_OK) {
		return ret;
	}

	vector<PendingVersion> pending;
	pending.reserve(items.size());
	for (const auto &item : items) {
		if (!valid_version(doc.kind, item.second)) {
			cerr << "VersionsFile. Invalid version!" << endl;
			return V8UNPACK_INVALID_VERSION;
		}

		int index = find_entry(doc, item.first);
		if (index < 0) {
			cerr << "VersionsFile. `" << item.first << "` not found!" << endl;
			return V8UNPACK_ELEM_NOT_FOUND;
		}

		pending.push_back({index, item.second});
	}

	apply_versions(doc, pending);
	return save_document(filename, doc);
}

int VersionsSet(const string &filename, const string &block_name, const string &version)
{
	return VersionsSet(filename, {make_pair(block_name, version)});
}

int VersionsUpdate(const string &filename, const vector<string> &names)
{
	if (names.empty()) {
		return V8UNPACK_SHOW_USAGE;
	}

	VersionsDocument doc;
	int ret = load_document(filename, doc);
	if (ret != V8UNPACK_OK) {
		return ret;
	}

	vector<PendingVersion> pending;
	pending.reserve(names.size());
	for (const auto &name : names) {
		int index = find_entry(doc, name);
		if (index < 0) {
			cerr << "VersionsFile. `" << name << "` not found!" << endl;
			return V8UNPACK_ELEM_NOT_FOUND;
		}

		string version = doc.kind == VersionsKind::Uuid ? generate_uuid() : generate_b64();
		pending.push_back({index, version});
	}

	auto printed = pending;
	apply_versions(doc, pending);
	ret = save_document(filename, doc);
	if (ret != V8UNPACK_OK) {
		return ret;
	}

	for (const auto &item : printed) {
		if (names.size() == 1) {
			cout << item.version << '\n';
		} else {
			const auto &name = doc.entries[static_cast<size_t>(item.index)].name;
			cout << name << "  " << item.version << '\n';
		}
	}
	cout << flush;
	return V8UNPACK_OK;
}

int VersionsUpdate(const string &filename, const string &block_name)
{
	return VersionsUpdate(filename, vector<string>{block_name});
}

}
