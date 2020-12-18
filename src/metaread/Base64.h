//---------------------------------------------------------------------------

#ifndef Base64H
#define Base64H


//---------------------------------------------------------------------------

void base64_encode(TStream* infile, TStream* outfile, int linesize);
void base64_decode(TStream* infile, TStream* outfile);
void base64_decode(const String& instr, TStream* outfile, int start = 0);

#endif

