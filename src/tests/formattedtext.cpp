/*
 *  tests/formattedtext.cpp
 *
 *  Copyright (C) 2011 - Niko Ritari
 *
 *  This file is part of Outgun.
 *
 *  Outgun is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Outgun is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Outgun; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <cstdlib>

#include "../utility.h"

#include "tests.h"

using namespace std;

string splitToLinesInput(int maxLength) {
    int targetLength = rand() % 40 + 1;
    if (targetLength >= 20)
        targetLength += rand() % 100;
    if (targetLength > maxLength)
        targetLength = rand() % maxLength + 1;

    string code, charsOnly;
    while ((int)code.length() < targetLength) {
        int wordLength = rand() % 40;
        if (wordLength >= 30)
            wordLength += rand() % 100;
        else if (wordLength >= 20)
            wordLength += rand() % 20;

        const bool space = rand() % 2;
        if (space) {
            code.append(wordLength, ' ');
            charsOnly.append(wordLength, ' ');
        }
        else
            for (; wordLength; --wordLength)
                if (rand() % 10) {
                    code += char('a' + rand() % 20);
                    charsOnly += *code.rbegin();
                }
                else {
                    code += "$$";
                    charsOnly += '$';
                }
    }

    int nFormatSpans;
    nFormatSpans = rand() % 4;
    if (nFormatSpans >= 3)
        nFormatSpans = rand() % 100;
    else if (nFormatSpans >= 2)
        nFormatSpans = rand() % 10;
    if (nFormatSpans > (int)code.length())
        nFormatSpans = rand() % code.length();

    for (; nFormatSpans; --nFormatSpans) {
        int openTagPos = -1;
        for (int open = 1; open >= 0; --open) { // the closing tag may end up closing a different opening tag but only the counts matter (and relative ordering a bit)
            int pos;
            do {
                pos = open ? rand() % code.length() : openTagPos + rand() % (code.length() - openTagPos);
                if (rand() % 2) { // put half the tags near a word boundary
                    const bool startSpace = code[pos] == ' ';
                    const int dist = rand() % 3 - 1;
                    const int dir = rand() % 2 ? +1 : -1;
                    while (pos >= 0 && pos < (int)code.length() && (code[pos] == ' ') == startSpace)
                        pos += dir;
                    if (pos < 0 || pos >= (int)code.length() || dir == -1)
                        pos -= dir;
                    pos += dir * dist;
                    if (pos < 0)
                        pos = 0;
                    else if (pos > (int)code.length())
                        pos = code.length() - 1;
                }
                if (pos > 0 && code[pos - 1] == '$') {
                    int dollars = 1;
                    for (int p = pos - 2; p >= 0 && code[p] == '$'; --p)
                        ++dollars;
                    if (dollars % 2)
                        --pos;
                }
            } while (!open && pos <= openTagPos);
            string tag = "$";
            if (!open)
                tag += '>';
            else {
                switch (rand() % 3) {
                /*break;*/ case 0: tag += 'R';
                    break; case 1: tag += 'G';
                    break; case 2: tag += 'B';
                }
                openTagPos = pos;
            }
            nAssert(pos >= 0 && pos <= (int)code.size());
            code.insert(pos, tag);
        }
    }

    const FormattedText f = FormattedText::parse(code);
    const string u = f.unformatted();
    nAssert(u == charsOnly);

    return code;
}

void checkStructure(const FormattedText& ft) {
    for (vector<FormattedText::Snippet>::const_iterator pi = ft.parts().begin(); pi != ft.parts().end(); ++pi)
        nAssert(!pi->text.empty());
}

void splitToLinesTest(const string& s, int lineLength, int indent, bool keepSpaces) {
    const FormattedText ft = FormattedText::parse(s);
    checkStructure(ft);
    const string text = ft.unformatted();
    const vector<string> textLines = split_to_lines(text, lineLength, indent, keepSpaces);
    const vector<FormattedText> fmtLines = split_to_lines(ft, lineLength, indent, keepSpaces);
    nAssert(textLines.size() == fmtLines.size());
    for (int i = 0; i < (int)textLines.size(); ++i) {
        checkStructure(fmtLines[i]);
        nAssert(textLines[i] == fmtLines[i].unformatted());
    }
}

void splitToLinesTests(int lineLength) {
    for (int keepSpaces = 0; keepSpaces <= 1; ++keepSpaces)
        for (int indent = 0; indent < lineLength; ++indent) {
            splitToLinesTest("", lineLength, indent, keepSpaces);
            for (int rep = 0; rep < 5; ++rep)
                splitToLinesTest(splitToLinesInput(2 * lineLength), lineLength, indent, keepSpaces);
            for (int rep = 0; rep < 10; ++rep)
                splitToLinesTest(splitToLinesInput(10 * lineLength), lineLength, indent, keepSpaces);
        }
}

int main() {
    srand(time(0));
    for (int len = 1; len <= 10; ++len)
        splitToLinesTests(len);
    for (int len = 15; len <= 50; len += 5)
        splitToLinesTests(len);
    return 0;
}
