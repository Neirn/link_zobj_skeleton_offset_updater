#include <fstream>
#include <iostream>
#include <filesystem>
#include <string>
#include <queue>
#include <cctype>
#include <unordered_map>

using namespace std;

const enum ERRORS {
	INVALID_ARGS,
	ZOBJ_FILE_NOT_FOUND,
	MANIFEST_FILE_NOT_FOUND,
	REPEATED_ARGS,
	ODD_ZOBJ_SIZE,
	EMPTY_OFFSET_MAP,
	MISSING_ARGS,
	SKELETON_OUT_OF_RANGE,
	SKELETON_NOT_MULTIPLE_OF_8,
	NOT_ENOUGH_INPUTS
};

/* Function prototypes */
ERRORS printError(ERRORS e);
unordered_map<long, long> processManifest(filesystem::path m);
void writeNBytes(char* buffer, unsigned long offset, uintmax_t bytesToWrite, int numBytes);
void write4Bytes(char* buffer, unsigned long offset, unsigned long bytesToWrite);
void write4Bytes(char* bufferStart, unsigned long bytesToWrite);
void write3Bytes(char* buffer, unsigned long offset, unsigned long bytesToWrite);
void write3Bytes(char* bufferStart, unsigned long bytesToWrite);
void printHelp();

const int NUM_LIMBS_LINK = 21;

const char* EXAMPLE_USAGE_STR1 = "-i model.zobj -m zzobjman_output.txt -s 0xE0A0";
const char* EXAMPLE_USAGE_STR2 = "-i model.zobj -o output.zobj -m zzobjman_output.txt -s 0xE0A0";
const char* GENERIC_HELP_PROMPT = "Enter -h to view help!";

int main(int argc, char* argv[]) {

	cout << "Welcome to my Link skeleton offset updater tool!\n";

	if (argc < 2) {
		return printError(NOT_ENOUGH_INPUTS);
	}

	filesystem::path zobjPath, manifestPath, outputPath;
	string skeletonOffsetStr;

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if (i + 1 >= argc)
				return printError(INVALID_ARGS);

			switch (tolower(argv[i][1])) {
			case 'i':
				if (!zobjPath.empty())
					return printError(REPEATED_ARGS);

				zobjPath = argv[i + 1];
				if (!filesystem::exists(zobjPath))
					return printError(ZOBJ_FILE_NOT_FOUND);

				break;

			case 'm':
				if (!manifestPath.empty())
					return printError(REPEATED_ARGS);

				manifestPath = argv[i + 1];
				if (!filesystem::exists(manifestPath))
					return printError(MANIFEST_FILE_NOT_FOUND);

				break;

			case 'o':
				if (!outputPath.empty())
					return printError(REPEATED_ARGS);
				
				outputPath = argv[i + 1];

				break;

			case 's':
				if (skeletonOffsetStr != "")
					return printError(REPEATED_ARGS);

				skeletonOffsetStr = argv[i + 1];
				
				break;

			case 'h':
				printHelp();
				return 0;
				break;

			default:
				return printError(INVALID_ARGS);
				break;
			}
		}
	}
	
	if (zobjPath.empty() || manifestPath.empty() || skeletonOffsetStr == "")
		return printError(INVALID_ARGS);

	if (outputPath.empty()) {
		outputPath = zobjPath;
		outputPath.replace_extension("");
		outputPath.concat("_repointed.zobj");
	}

	unordered_map<long, long> offsetMap = processManifest(manifestPath);

	if (offsetMap.empty()) {
		return printError(EMPTY_OFFSET_MAP);
	}

	long zobjSize = filesystem::file_size(zobjPath);

	if ((zobjSize % 8) != 0) {
		cout << "Input file's size is not a multiple of 8! Are you sure it's a zobj?\n";
		return printError(ODD_ZOBJ_SIZE);
	}

	long skeletonOffset = stol(skeletonOffsetStr, nullptr, 16);

	if (skeletonOffset % 8 != 0)
		return printError(SKELETON_NOT_MULTIPLE_OF_8);

	if (skeletonOffset + (16 * NUM_LIMBS_LINK) + (4 * NUM_LIMBS_LINK) >= zobjSize) {
		return printError(SKELETON_OUT_OF_RANGE);
	}

	fstream zobj = fstream(zobjPath, ios::in | ios::out | ios::binary);

	zobj.seekg(ios::beg);

	char* buffer = new char[zobjSize];

	zobj.read(buffer, zobjSize);

	unordered_map<long, long>::iterator it;

	queue<string> zzManifestQ;

	for (unsigned long i = 0, startOfPtr, dlPtr, msb, b, lsb, limbEntryStart; i < NUM_LIMBS_LINK; i++) {

		limbEntryStart = skeletonOffset + (16 * i);
		startOfPtr = limbEntryStart + 8;

		if (buffer[startOfPtr] == 0x06) {
			msb = (unsigned char)buffer[startOfPtr + 1];

			b = (unsigned char)buffer[startOfPtr + 2];

			lsb = (unsigned char)buffer[startOfPtr + 3];

			dlPtr = (msb << 16) | (b << 8) | lsb;

			it = offsetMap.find(dlPtr);

			if (it != offsetMap.end()) {
				// overwrite the high poly model ptr and the low poly model ptr

				unsigned long newOff = it->second;

				write3Bytes(buffer, startOfPtr + 1, newOff);
				write3Bytes(buffer, startOfPtr + 5, newOff);

				unsigned long newOffMSB{ newOff >> 16 }, newOffB{ (newOff >> 8) & 0xFF }, newOffLSB{ newOff & 0xFF };

				zzManifestQ.push("Limb " + to_string(i) + (char)0 + char(0) + (char)newOffMSB + (char)newOffB + (char)newOffLSB);

			}
		}

		// update pointers to skeleton limb entries
		unsigned long offset = skeletonOffset + 0x150 + 1 + (4 * i);
		write3Bytes(buffer, offset, limbEntryStart);

	}

	// last skeleton pointer
	write3Bytes(buffer, skeletonOffset + 0x150 + (4 * NUM_LIMBS_LINK) + 1, skeletonOffset + 0x150);

	filesystem::path zzManifestPath = outputPath;
	zzManifestPath.replace_extension("");
	zzManifestPath.concat("_zzmanifest.bin");

	string zzManifestCombined;

	while (!zzManifestQ.empty()) {

		string currString = zzManifestQ.front();

		zzManifestCombined += currString;

		zzManifestQ.pop();
	}

	ofstream out = ofstream(outputPath, ios::binary);
	ofstream outManifest = ofstream(zzManifestPath, ios::binary);

	out.write(buffer, zobjSize);
	outManifest.write(zzManifestCombined.c_str(), zzManifestCombined.size());

	cout << "zobj edited successfully!\n";

	delete[] buffer;

}

ERRORS printError(ERRORS e) {
	switch (e) {
	case INVALID_ARGS:
		cout << "Invalid arguments.";
		break;
	case ZOBJ_FILE_NOT_FOUND:
		cout << "Couldn't find zobj input file!";
		break;
	case MANIFEST_FILE_NOT_FOUND:
		cout << "Couldn't find zzobjman manifest!";
		break;
	case REPEATED_ARGS:
		cout << "One or more repeated arguments detected!";
		break;
	case ODD_ZOBJ_SIZE:
		cout << "zobj file size was not a multiple of 8!\nAre you sure it's a zobj?";
		break;
	case EMPTY_OFFSET_MAP:
		cout << "Couldn't find any converted display lists in your manifest!";
		break;
	case MISSING_ARGS:
		cout << "Missing one or more arguments!";
		break;
	case SKELETON_OUT_OF_RANGE:
		cout << "Skeleton offset was not within the zobj's size!";
		break;
	case SKELETON_NOT_MULTIPLE_OF_8:
		cout << "Skeleton offset was not a multiple of 8!\nDouble check your offset!";
		break;
	case NOT_ENOUGH_INPUTS:
		cout << "Not enough inputs!";
		break;
	default:
		cout << "Unimplemented error message.";
		break;
	}

	cout << '\n' << GENERIC_HELP_PROMPT << '\n';

	return e;
}

unordered_map<long, long> processManifest(filesystem::path m) {

	string line;
	ifstream manifest(m);

	unordered_map<long, long> result;

	while (getline(manifest, line)) {
		if (line.length() == 20 && line.substr(0, 2) == "0x") {
			long key = stol(line.substr(0, 8), nullptr, 16);
			long val = stol(line.substr(12, 8), nullptr, 16);
			result.insert(std::make_pair(key, val));
		}
	}

	return result;
}

void write4Bytes(char* buffer, unsigned long offset, unsigned long bytesToWrite) {
	writeNBytes(buffer, offset, bytesToWrite, 4);
}

void write4Bytes(char* bufferStart, unsigned long bytesToWrite) {
	write4Bytes(bufferStart, 0, bytesToWrite);
}

void write3Bytes(char* buffer, unsigned long offset, unsigned long bytesToWrite) {
	writeNBytes(buffer, offset, bytesToWrite, 3);
}

void write3Bytes(char* bufferStart, unsigned long bytesToWrite) {
	write3Bytes(bufferStart, 0, bytesToWrite);
}

void writeNBytes(char* buffer, unsigned long offset, uintmax_t bytesToWrite, int numBytes) {

	if (numBytes > sizeof(uintmax_t))
		return;

	for (int i = 0; i < numBytes; i++) {
		buffer[offset + i] = (bytesToWrite >> (8 * (numBytes - 1 - i))) & 0xFF;
	}
}

void printHelp() {
	cout << "Usage:\nRun your zobj with Link's hierarchy through zzobjman's optimize function, including all display lists attached to the hierarchy.\n";
	cout << "Make sure your new zobj's size accounts for the offset arg of zzobjman.\n";
	cout << "Copy the limb entries followed by the limb indexes followed by the skeleton header from the original zobj to your optimized one IN THAT ORDER AND WITH NO GAPS BETWEEN THEM.\n";
	cout << "Now run your new optimized zobj through this program.\nAlso, don't forget to copy over the first 0x5000 bytes of your original model " <<
		"file if you want to carry over your mouth and eye textures.\n\n";
	cout << "Arguments:\n";
	cout << "-i <path to your zobj here>\n";
	cout << "-m <path to the output from zzobjman's optimize command>\n";
	cout << "-o <output name> (optional)\n";
	cout << "-s <offset of the first limb entry (i.e. limb 00)\n";

	cout << "Example arguments:\n" << EXAMPLE_USAGE_STR1 << '\n' << EXAMPLE_USAGE_STR2 << '\n';
}
