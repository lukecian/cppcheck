/*
 * Cppcheck - A tool for static C/C++ code analysis
 * Copyright (C) 2007-2016 Cppcheck team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

//---------------------------------------------------------------------------
// Buffer overrun..
//---------------------------------------------------------------------------

#include "checkbufferoverrun.h"

#include "tokenize.h"
#include "mathlib.h"
#include "symboldatabase.h"
#include "astutils.h"

#include <algorithm>
#include <sstream>
#include <list>
#include <cassert>     // <- assert
#include <cstdlib>
#include <stack>

//---------------------------------------------------------------------------

// Register this check class (by creating a static instance of it)
namespace {
    CheckBufferOverrun instance;
}

//---------------------------------------------------------------------------

static void makeArrayIndexOutOfBoundsError(std::ostream& oss, const CheckBufferOverrun::ArrayInfo &arrayInfo, const std::vector<MathLib::bigint> &index)
{
    oss << "Array '" << arrayInfo.varname();
    for (std::size_t i = 0; i < arrayInfo.num().size(); ++i)
        oss << "[" << arrayInfo.num(i) << "]";
    if (index.size() == 1)
        oss << "' accessed at index " << index[0] << ", which is";
    else {
        oss << "' index " << arrayInfo.varname();
        for (std::size_t i = 0; i < index.size(); ++i)
            oss << "[" << index[i] << "]";
    }
    oss << " out of bounds.";
}
void CheckBufferOverrun::arrayIndexOutOfBoundsError(const Token *tok, const ArrayInfo &arrayInfo, const std::vector<MathLib::bigint> &index)
{
    std::ostringstream oss;
    makeArrayIndexOutOfBoundsError(oss, arrayInfo, index);
    reportError(tok, Severity::error, "arrayIndexOutOfBounds", oss.str(), 788U, false);
}

void CheckBufferOverrun::arrayIndexOutOfBoundsError(const Token *tok, const ArrayInfo &arrayInfo, const std::vector<ValueFlow::Value> &index)
{
    const Token *condition = nullptr;
    for (std::size_t i = 0; i < index.size(); ++i) {
        if (condition == nullptr)
            condition = index[i].condition;
    }

    if (condition != nullptr) {
        if (!_settings->isEnabled("warning"))
            return;

        std::ostringstream errmsg;
        errmsg << ValueFlow::eitherTheConditionIsRedundant(condition) << " or the array '" << arrayInfo.varname();
        for (std::size_t i = 0; i < arrayInfo.num().size(); ++i)
            errmsg << "[" << arrayInfo.num(i) << "]";
        if (index.size() == 1)
            errmsg << "' is accessed at index " << index[0].intvalue << ", which is out of bounds.";
        else {
            errmsg << "' index " << arrayInfo.varname();
            for (std::size_t i = 0; i < index.size(); ++i)
                errmsg << "[" << index[i].intvalue << "]";
            errmsg << " is out of bounds.";
        }

        std::list<const Token *> callstack;
        callstack.push_back(tok);
        callstack.push_back(condition);
        reportError(callstack, Severity::warning, "arrayIndexOutOfBoundsCond", errmsg.str(), 0U, false);
    } else {
        std::ostringstream errmsg;
        errmsg << "Array '" << arrayInfo.varname();
        for (std::size_t i = 0; i < arrayInfo.num().size(); ++i)
            errmsg << "[" << arrayInfo.num(i) << "]";
        if (index.size() == 1)
            errmsg << "' accessed at index " << index[0].intvalue << ", which is out of bounds.";
        else {
            errmsg << "' index " << arrayInfo.varname();
            for (std::size_t i = 0; i < index.size(); ++i)
                errmsg << "[" << index[i].intvalue << "]";
            errmsg << " out of bounds.";
        }

        reportError(tok, Severity::error, "arrayIndexOutOfBounds", errmsg.str());
    }
}

void CheckBufferOverrun::arrayIndexOutOfBoundsError(const std::list<const Token *> &callstack, const ArrayInfo &arrayInfo, const std::vector<MathLib::bigint> &index)
{
    std::ostringstream oss;
    makeArrayIndexOutOfBoundsError(oss, arrayInfo, index);
    reportError(callstack, Severity::error, "arrayIndexOutOfBounds", oss.str());
}

static std::string bufferOverrunMessage(std::string varnames)
{
    varnames.erase(std::remove(varnames.begin(), varnames.end(), ' '), varnames.end());

    std::string errmsg("Buffer is accessed out of bounds");
    if (!varnames.empty())
        errmsg += ": " + varnames;
    else
        errmsg += ".";

    return errmsg;
}

void CheckBufferOverrun::bufferOverrunError(const Token *tok, const std::string &varnames)
{
    reportError(tok, Severity::error, "bufferAccessOutOfBounds", bufferOverrunMessage(varnames), 788U, false);
}


void CheckBufferOverrun::bufferOverrunError(const std::list<const Token *> &callstack, const std::string &varnames)
{
    reportError(callstack, Severity::error, "bufferAccessOutOfBounds", bufferOverrunMessage(varnames));
}

void CheckBufferOverrun::possibleBufferOverrunError(const Token *tok, const std::string &src, const std::string &dst, bool cat)
{
    if (cat)
        reportError(tok, Severity::warning, "possibleBufferAccessOutOfBounds",
                    "Possible buffer overflow if strlen(" + src + ") is larger than sizeof(" + dst + ")-strlen(" + dst +").\n"
                    "Possible buffer overflow if strlen(" + src + ") is larger than sizeof(" + dst + ")-strlen(" + dst +"). "
                    "The source buffer is larger than the destination buffer so there is the potential for overflowing the destination buffer.");
    else
        reportError(tok, Severity::warning, "possibleBufferAccessOutOfBounds",
                    "Possible buffer overflow if strlen(" + src + ") is larger than or equal to sizeof(" + dst + ").\n"
                    "Possible buffer overflow if strlen(" + src + ") is larger than or equal to sizeof(" + dst + "). "
                    "The source buffer is larger than the destination buffer so there is the potential for overflowing the destination buffer.");
}

void CheckBufferOverrun::strncatUsageError(const Token *tok)
{
    if (_settings && !_settings->isEnabled("warning"))
        return;

    reportError(tok, Severity::warning, "strncatUsage",
                "Dangerous usage of strncat - 3rd parameter is the maximum number of characters to append.\n"
                "At most, strncat appends the 3rd parameter's amount of characters and adds a terminating null byte.\n"
                "The safe way to use strncat is to subtract one from the remaining space in the buffer and use it as 3rd parameter."
                "Source: http://www.cplusplus.com/reference/cstring/strncat/\n"
                "Source: http://www.opensource.apple.com/source/Libc/Libc-167/gen.subproj/i386.subproj/strncat.c");
}

void CheckBufferOverrun::outOfBoundsError(const Token *tok, const std::string &what, const bool show_size_info, const MathLib::bigint &supplied_size, const MathLib::bigint &actual_size)
{
    std::ostringstream oss;

    oss << what << " is out of bounds";
    if (show_size_info)
        oss << ": Supplied size " << supplied_size << " is larger than actual size " << actual_size;
    oss << '.';
    reportError(tok, Severity::error, "outOfBounds", oss.str(), 788U, false);
}

void CheckBufferOverrun::pointerOutOfBoundsError(const Token *tok, const Token *index, const MathLib::bigint indexvalue)
{
    // The severity is portability instead of error since this ub doesnt
    // cause bad behaviour on most implementations. people create out
    // of bounds pointers by intention.
    const std::string expr(tok ? tok->expressionString() : std::string(""));
    std::string errmsg;
    if (index && !index->isNumber()) {
        errmsg = "Undefined behaviour, when '" +
                 index->expressionString() +
                 "' is " +
                 MathLib::toString(indexvalue) +
                 " the pointer arithmetic '" + expr + "' is out of bounds";
    } else {
        errmsg = "Undefined behaviour, pointer arithmetic '" + expr + "' is out of bounds";
    }
    std::string verbosemsg(errmsg + ". From chapter 6.5.6 in the C specification:\n"
                           "\"When an expression that has integer type is added to or subtracted from a pointer, ..\" and then \"If both the pointer operand and the result point to elements of the same array object, or one past the last element of the array object, the evaluation shall not produce an overflow; otherwise, the behavior is undefined.\"");
    reportError(tok, Severity::portability, "pointerOutOfBounds", errmsg + ".\n" + verbosemsg);
    /*
         "Undefined behaviour: The result of this pointer arithmetic does not point into
         or just one element past the end of the " + object + ".
         Further information:
          https://www.securecoding.cert.org/confluence/display/seccode/ARR30-C.+Do+not+form+or+use+out+of+bounds+pointers+or+array+subscripts");
    */
}

void CheckBufferOverrun::sizeArgumentAsCharError(const Token *tok)
{
    if (_settings && !_settings->isEnabled("warning"))
        return;
    reportError(tok, Severity::warning, "sizeArgumentAsChar", "The size argument is given as a char constant.");
}


void CheckBufferOverrun::terminateStrncpyError(const Token *tok, const std::string &varname)
{
    reportError(tok, Severity::warning, "terminateStrncpy",
                "The buffer '" + varname + "' may not be null-terminated after the call to strncpy().\n"
                "If the source string's size fits or exceeds the given size, strncpy() does not add a "
                "zero at the end of the buffer. This causes bugs later in the code if the code "
                "assumes buffer is null-terminated.", 0U, true);
}

void CheckBufferOverrun::cmdLineArgsError(const Token *tok)
{
    reportError(tok, Severity::error, "insecureCmdLineArgs", "Buffer overrun possible for long command line arguments.");
}

void CheckBufferOverrun::bufferNotZeroTerminatedError(const Token *tok, const std::string &varname, const std::string &function)
{
    const std::string errmsg = "The buffer '" + varname + "' is not null-terminated after the call to " + function + "().\n"
                               "The buffer '" + varname + "' is not null-terminated after the call to " + function + "(). "
                               "This will cause bugs later in the code if the code assumes the buffer is null-terminated.";

    reportError(tok, Severity::warning, "bufferNotZeroTerminated", errmsg, 0U, true);
}

void CheckBufferOverrun::argumentSizeError(const Token *tok, const std::string &functionName, const std::string &varname)
{
    reportError(tok, Severity::warning, "argumentSize", "The array '" + varname + "' is too small, the function '" + functionName + "' expects a bigger one.");
}

void CheckBufferOverrun::negativeMemoryAllocationSizeError(const Token *tok)
{
    reportError(tok, Severity::error, "negativeMemoryAllocationSize",
                "Memory allocation size is negative.\n"
                "Memory allocation size is negative."
                "Negative allocation size has no specified behaviour.");
}

//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// Check array usage..
//---------------------------------------------------------------------------


static bool isAddressOf(const Token *tok)
{
    const Token *tok2 = tok->astParent();
    while (Token::Match(tok2, "%name%|.|::|["))
        tok2 = tok2->astParent();
    return tok2 && tok2->str() == "&" && !(tok2->astOperand1() && tok2->astOperand2());
}

/**
 * bailout if variable is used inside if/else/switch block or if there is "break"
 * @param tok token for "if" or "switch"
 * @param varid variable id
 * @return is bailout recommended?
 */
static bool bailoutIfSwitch(const Token *tok, const unsigned int varid)
{
    const Token* end = tok->linkAt(1)->linkAt(1);
    if (Token::simpleMatch(end, "} else {")) // scan the else-block
        end = end->linkAt(2);
    if (Token::simpleMatch(end, "{")) // Ticket #5203: Invalid code, bailout
        return true;

    // Used later to check if the body belongs to a "if"
    const bool is_if = tok->str() == "if";

    for (; tok && tok != end; tok = tok->next()) {
        // If scanning a "if" block then bailout for "break"
        if (is_if && (tok->str() == "break" || tok->str() == "continue"))
            return true;

        // bailout for "return"
        else if (tok->str() == "return")
            return true;

        // bailout if varid is found
        else if (tok->varId() == varid)
            return true;
    }

    // No bailout stuff found => return false
    return false;
}
//---------------------------------------------------------------------------

static bool checkMinSizes(const std::list<Library::ArgumentChecks::MinSize> &minsizes, const Token * const ftok, const MathLib::bigint arraySize, const Token **charSizeToken, const Settings * const settings)
{
    if (charSizeToken)
        *charSizeToken = nullptr;

    if (minsizes.empty())
        return false;

    // All conditions must be true
    bool error = true;
    for (std::list<Library::ArgumentChecks::MinSize>::const_iterator minsize = minsizes.begin(); minsize != minsizes.end(); ++minsize) {
        if (!error)
            return false;
        error = false;
        const Token *argtok = ftok->tokAt(2);
        for (int argnum = 1; argtok && argnum < minsize->arg; argnum++)
            argtok = argtok->nextArgument();
        if (!argtok)
            return false;
        switch (minsize->type) {
        case Library::ArgumentChecks::MinSize::ARGVALUE:
            if (Token::Match(argtok, "%num% ,|)")) {
                const MathLib::bigint sz = MathLib::toLongNumber(argtok->str());
                if (sz > arraySize)
                    error = true;
            } else if (argtok->tokType() == Token::eChar && Token::Match(argtok->next(), ",|)") && charSizeToken)
                *charSizeToken = argtok; //sizeArgumentAsCharError(argtok);
            break;
        case Library::ArgumentChecks::MinSize::MUL:
            // TODO: handle arbitrary arg2
            if (minsize->arg2 == minsize->arg+1 && Token::Match(argtok, "%num% , %num% ,|)")) {
                const MathLib::bigint sz = MathLib::toLongNumber(argtok->str()) * MathLib::toLongNumber(argtok->strAt(2));
                if (sz > arraySize)
                    error = true;
            }
            break;
        case Library::ArgumentChecks::MinSize::STRLEN: {
            if (settings->library.isargformatstr(ftok,minsize->arg)) {
                std::list<const Token *> parameters;
                const std::string &formatstr(argtok->str());
                const Token *argtok2 = argtok;
                while ((argtok2 = argtok2->nextArgument()) != nullptr) {
                    if (Token::Match(argtok2, "%num%|%str% [,)]"))
                        parameters.push_back(argtok2);
                    else
                        parameters.push_back(nullptr);
                }
                const MathLib::biguint len = CheckBufferOverrun::countSprintfLength(formatstr, parameters);
                if (len > arraySize + 2U)
                    error = true;
            } else {
                const Token *strtoken = argtok->getValueTokenMaxStrLength();
                if (strtoken && Token::getStrLength(strtoken) >= arraySize)
                    error = true;
            }
        }
        break;
        case Library::ArgumentChecks::MinSize::SIZEOF:
            if (argtok->tokType() == Token::eString && Token::getStrLength(argtok) >= arraySize)
                error = true;
            break;
        case Library::ArgumentChecks::MinSize::NONE:
            return false;
        };
    }
    return error;
}

void CheckBufferOverrun::checkFunctionParameter(const Token &ftok, unsigned int paramIndex, const ArrayInfo &arrayInfo, const std::list<const Token *>& callstack)
{
    const std::list<Library::ArgumentChecks::MinSize> * const minsizes = _settings->library.argminsizes(&ftok, paramIndex);

    if (minsizes && (!(Token::simpleMatch(ftok.previous(), ".") || Token::Match(ftok.tokAt(-2), "!!std ::")))) {
        MathLib::bigint arraySize = arrayInfo.element_size();
        if (arraySize == 0)
            return;
        for (std::size_t i = 0; i < arrayInfo.num().size(); ++i)
            arraySize *= arrayInfo.num(i);

        // dimension is 0 or unknown => bailout
        if (arraySize == 0)
            return;

        const Token *charSizeToken = nullptr;
        if (checkMinSizes(*minsizes, &ftok, arraySize, &charSizeToken, _settings))
            bufferOverrunError(callstack, arrayInfo.varname());
        if (charSizeToken)
            sizeArgumentAsCharError(charSizeToken);
    }

    // Calling a user function?
    // only 1-dimensional arrays can be checked currently
    else if (arrayInfo.num().size() == 1) {
        const Function* const func = ftok.function();

        if (func && func->hasBody()) {
            // Get corresponding parameter..
            const Variable* const parameter = func->getArgumentVar(paramIndex-1);

            // Ensure that it has a compatible size..
            if (!parameter || _tokenizer->sizeOfType(parameter->typeStartToken()) != arrayInfo.element_size())
                return;

            // No variable id occur for instance when:
            // - Variable function arguments: "void f(...)"
            // - Unnamed parameter: "void f(char *)"
            if (parameter->declarationId() == 0)
                return;

            // Check the parameter usage in the function scope..
            for (const Token* ftok2 = func->functionScope->classStart; ftok2 != func->functionScope->classEnd; ftok2 = ftok2->next()) {
                if (Token::Match(ftok2, "if|for|switch|while (")) {
                    // bailout if there is buffer usage..
                    if (bailoutIfSwitch(ftok2, parameter->declarationId())) {
                        break;
                    }

                    // no bailout is needed. skip the if-block
                    else {
                        // goto end of if block..
                        ftok2 = ftok2->linkAt(1)->linkAt(1);
                        if (Token::simpleMatch(ftok2, "} else {"))
                            ftok2 = ftok2->linkAt(2);
                        if (!ftok2)
                            break;
                        continue;
                    }
                }

                if (ftok2->str() == "}")
                    break;

                if (ftok2->varId() == parameter->declarationId()) {
                    if (Token::Match(ftok2->previous(), "-- %name%") ||
                        Token::Match(ftok2, "%name% --"))
                        break;

                    if (Token::Match(ftok2->previous(), ";|{|}|%op% %name% [ %num% ]")) {
                        const MathLib::bigint index = MathLib::toLongNumber(ftok2->strAt(2));
                        if (index >= 0 && arrayInfo.num(0) > 0 && index >= arrayInfo.num(0)) {
                            std::list<const Token *> callstack2(callstack);
                            callstack2.push_back(ftok2);

                            std::vector<MathLib::bigint> indexes;
                            indexes.push_back(index);

                            arrayIndexOutOfBoundsError(callstack2, arrayInfo, indexes);
                        }
                    }
                }

                // Calling function..
                if (Token::Match(ftok2, "%name% (")) {
                    ArrayInfo ai(arrayInfo);
                    ai.declarationId(parameter->declarationId());
                    checkFunctionCall(ftok2, ai, callstack);
                }
            }
        }
    }

    // Check 'float x[10]' arguments in declaration
    if (_settings->isEnabled("warning")) {
        const Function* const func = ftok.function();

        // If argument is '%type% a[num]' then check bounds against num
        if (func) {
            const Variable* const argument = func->getArgumentVar(paramIndex-1);
            const Token *nameToken;
            if (argument && Token::Match(argument->typeStartToken(), "%type% %var% [ %num% ] [,)[]")
                && (nameToken = argument->nameToken()) != nullptr) {
                const Token *tok2 = nameToken->next();

                MathLib::bigint argsize = _tokenizer->sizeOfType(argument->typeStartToken());
                if (argsize == 100) // unknown size
                    argsize = 0;
                do {
                    argsize *= MathLib::toLongNumber(tok2->strAt(1));
                    tok2 = tok2->tokAt(3);
                } while (Token::Match(tok2, "[ %num% ] [,)[]"));

                MathLib::bigint arraysize = arrayInfo.element_size();
                if (arraysize == 100) // unknown size
                    arraysize = 0;
                for (std::size_t i = 0; i < arrayInfo.num().size(); i++)
                    arraysize *= arrayInfo.num(i);

                if (Token::Match(tok2, "[,)]") && arraysize > 0 && argsize > arraysize)
                    argumentSizeError(&ftok, ftok.str(), arrayInfo.varname());
            }
        }
    }
}


void CheckBufferOverrun::checkFunctionCall(const Token *tok, const ArrayInfo &arrayInfo, std::list<const Token *> callstack)
{
    // Don't go deeper than 2 levels, the checking can get very slow
    // when there is no limit
    if (callstack.size() >= 2)
        return;

    // Prevent recursion
    for (std::list<const Token*>::const_iterator it = callstack.begin(); it != callstack.end(); ++it) {
        // Same function name => bail out
        if (tok->str() == (*it)->str())
            return;
    }
    callstack.push_back(tok);

    const unsigned int declarationId = arrayInfo.declarationId();

    const Token *argtok = tok->tokAt(2);
    unsigned int argnr = 1U;
    while (argtok) {
        if (Token::Match(argtok, "%varid% ,|)", declarationId))
            checkFunctionParameter(*tok, argnr, arrayInfo, callstack);
        else if (Token::Match(argtok, "%varid% + %num% ,|)", declarationId)) {
            const ArrayInfo ai(arrayInfo.limit(MathLib::toLongNumber(argtok->strAt(2))));
            checkFunctionParameter(*tok, argnr, ai, callstack);
        }
        // goto next parameter..
        argtok = argtok->nextArgument();
        argnr++;
    }
}

void CheckBufferOverrun::checkScope(const Token *tok, const std::vector<std::string> &varname, const ArrayInfo &arrayInfo)
{
    const MathLib::bigint size = arrayInfo.num(0);
    if (size <= 0)  // unknown size
        return;

    if (tok->str() == "return") {
        tok = tok->next();
        if (!tok)
            return;
    }

    const bool printInconclusive = _settings->inconclusive;
    const MathLib::bigint total_size = arrayInfo.element_size() * size;
    const unsigned int declarationId = arrayInfo.declarationId();

    std::string varnames;
    for (std::size_t i = 0; i < varname.size(); ++i)
        varnames += (i == 0 ? "" : " . ") + varname[i];

    const unsigned char varcount = static_cast<unsigned char>(varname.empty() ? 0U : (varname.size() - 1) * 2U);

    // ValueFlow array index..
    if ((declarationId > 0 && Token::Match(tok, "%varid% [", declarationId)) ||
        (declarationId == 0 && Token::simpleMatch(tok, (varnames + " [").c_str()))) {

        const Token *tok2 = tok->next();
        while (tok2->str() != "[")
            tok2 = tok2->next();
        valueFlowCheckArrayIndex(tok2, arrayInfo);
    }

    // If the result of pointer arithmetic means that the pointer is
    // out of bounds then this flag will be set.
    bool pointerIsOutOfBounds = false;

    const bool printPortability = _settings->isEnabled("portability");

    for (const Token* const end = tok->scope()->classEnd; tok && tok != end; tok = tok->next()) {
        if (declarationId != 0 && Token::Match(tok, "%varid% = new|malloc|realloc", declarationId)) {
            // Abort
            break;
        }

        // reassign buffer
        if (declarationId > 0 && Token::Match(tok, "[;{}] %varid% = %any%", declarationId)) {
            // using varid .. bailout
            if (tok->tokAt(3)->varId() != declarationId)
                break;
            pointerIsOutOfBounds = false;
        }

        // Array index..
        if ((declarationId > 0 && ((tok->str() == "return" || (!tok->isName() && !Token::Match(tok, "[.&]"))) && Token::Match(tok->next(), "%varid% [", declarationId))) ||
            (declarationId == 0 && ((tok->str() == "return" || (!tok->isName() && !Token::Match(tok, "[.&]"))) && (Token::Match(tok->next(), (varnames + " [").c_str()) || Token::Match(tok->next(), (varname[0] +" [ %num% ] . " + varname[1] + " [ %num% ]").c_str()))))) {
            std::vector<MathLib::bigint> indexes;
            const Token *tok2 = tok->tokAt(2 + varcount);
            for (; Token::Match(tok2, "[ %num% ]"); tok2 = tok2->tokAt(3)) {
                const MathLib::bigint index = MathLib::toLongNumber(tok2->strAt(1));
                indexes.push_back(index);
            }
            for (; Token::Match(tok2->tokAt(3), "[ %num% ]"); tok2 = tok2->tokAt(3)) {
                const MathLib::bigint index = MathLib::toLongNumber(tok2->strAt(4));
                indexes.push_back(index);
            }
            if (indexes.empty() && arrayInfo.num().size() == 1U && Token::simpleMatch(tok2, "[") && tok2->astOperand2()) {
                const ValueFlow::Value *value = tok2->astOperand2()->getMaxValue(false);
                if (value) {
                    indexes.push_back(value->intvalue);
                }
            }

            if (indexes.size() == arrayInfo.num().size()) {
                // Check if the indexes point outside the whole array..
                // char a[10][10];
                // a[0][20]  <-- ok.
                // a[9][20]  <-- error.

                // total number of elements of array..
                MathLib::bigint totalElements = 1;

                // total index..
                MathLib::bigint totalIndex = 0;

                // calculate the totalElements and totalIndex..
                for (std::size_t i = 0; i < indexes.size(); ++i) {
                    const std::size_t ri = indexes.size() - 1 - i;
                    totalIndex += indexes[ri] * totalElements;
                    totalElements *= arrayInfo.num(ri);
                    if (arrayInfo.num(ri) == -1) {
                        // unknown size
                        totalElements = 0;
                        break;
                    }
                }

                // totalElements == 0 => Unknown size
                if (totalElements == 0)
                    continue;

                const Token *tok3 = tok->previous();
                while (tok3 && Token::Match(tok3->previous(), "%name% ."))
                    tok3 = tok3->tokAt(-2);

                // taking address of 1 past end?
                if (totalIndex == totalElements) {
                    const bool addr = (tok3 && (tok3->str() == "&" ||
                                                Token::simpleMatch(tok3->previous(), "& (")));
                    if (addr)
                        continue;
                }

                // Is totalIndex in bounds?
                if (totalIndex > totalElements || totalIndex < 0) {
                    arrayIndexOutOfBoundsError(tok->tokAt(1 + varcount), arrayInfo, indexes);
                }
                // Is any array index out of bounds?
                else {
                    // check each index for overflow
                    for (std::size_t i = 0; i < indexes.size(); ++i) {
                        if (indexes[i] >= arrayInfo.num(i)) {
                            if (indexes.size() == 1U) {
                                arrayIndexOutOfBoundsError(tok->tokAt(1 + varcount), arrayInfo, indexes);
                                break; // only warn about the first one
                            }

                            // The access is still within the memory range for the array
                            // so it may be intentional.
                            else if (printInconclusive) {
                                arrayIndexOutOfBoundsError(tok->tokAt(1 + varcount), arrayInfo, indexes);
                                break; // only warn about the first one
                            }
                        }
                    }
                }
            }
            tok = tok2;
            continue;
        }

        // memset, memcmp, memcpy, strncpy, fgets..
        if (declarationId == 0 && size > 0) {
            std::list<const Token *> callstack;
            callstack.push_back(tok);
            if (Token::Match(tok, ("%name% ( " + varnames + " ,").c_str()))
                checkFunctionParameter(*tok, 1, arrayInfo, callstack);
            if (Token::Match(tok, ("%name% ( %name% , " + varnames + " ,").c_str()))
                checkFunctionParameter(*tok, 2, arrayInfo, callstack);
        }

        if (total_size > 0) {
            // Writing data into array..
            if ((declarationId > 0 && Token::Match(tok, "strcpy|strcat ( %varid% , %str%|%var% )", declarationId)) ||
                (declarationId == 0 && Token::Match(tok, ("strcpy|strcat ( " + varnames + " , %str%|%var% )").c_str()))) {
                const Token* lastParamTok = tok->tokAt(varcount + 4);
                if (lastParamTok->tokType() == Token::Type::eString) {
                    const std::size_t len = Token::getStrLength(lastParamTok);
                    if (len >= total_size) {
                        bufferOverrunError(tok, declarationId > 0 ? emptyString : varnames);
                        continue;
                    }
                } else {
                    const Variable *var = lastParamTok->variable();
                    if (var && var->isArray() && var->dimensions().size() == 1) {
                        const MathLib::bigint len = var->dimension(0);
                        if (len > total_size) {
                            if (printInconclusive)
                                possibleBufferOverrunError(tok, tok->strAt(4), tok->strAt(2), tok->str() == "strcat");
                            continue;
                        }
                    }
                }
            }

            // Detect few strcat() calls
            const std::string strcatPattern = declarationId > 0 ? std::string("strcat ( %varid% , %str% ) ;") : ("strcat ( " + varnames + " , %str% ) ;");
            if (Token::Match(tok, strcatPattern.c_str(), declarationId)) {
                std::size_t charactersAppend = 0;
                const Token *tok2 = tok;

                do {
                    charactersAppend += Token::getStrLength(tok2->tokAt(4 + varcount));
                    if (charactersAppend >= static_cast<std::size_t>(total_size)) {
                        bufferOverrunError(tok2);
                        break;
                    }
                    tok2 = tok2->tokAt(7 + varcount);
                } while (Token::Match(tok2, strcatPattern.c_str(), declarationId));
            }

            // Check function call..
            if (Token::Match(tok, "%name% (")) {
                // No varid => function calls are not handled
                if (declarationId == 0)
                    continue;

                const ArrayInfo arrayInfo1(declarationId, varnames, total_size / size, size);
                const std::list<const Token *> callstack;
                checkFunctionCall(tok, arrayInfo1, callstack);
            }
        }

        // undefined behaviour: result of pointer arithmetic is out of bounds
        if (declarationId && Token::Match(tok, "= %varid% + %num% ;", declarationId)) {
            const MathLib::bigint index = MathLib::toLongNumber(tok->strAt(3));
            if (printPortability && index > size)
                pointerOutOfBoundsError(tok->tokAt(2));
            if (index >= size && Token::Match(tok->tokAt(-2), "[;{}] %varid% =", declarationId))
                pointerIsOutOfBounds = true;
        }

        else if (pointerIsOutOfBounds && Token::Match(tok, "[;{}=] * %varid% [;=]", declarationId)) {
            outOfBoundsError(tok->tokAt(2), tok->strAt(2), false, 0, 0);
        }
    }
}

static std::vector<ValueFlow::Value> valueFlowGetArrayIndexes(const Token * const tok, bool conditional, std::size_t dimensions)
{
    unsigned int indexvarid = 0;
    const std::vector<ValueFlow::Value> empty;
    std::vector<ValueFlow::Value> indexes;
    for (const Token *tok2 = tok; indexes.size() < dimensions && Token::Match(tok2, "["); tok2 = tok2->link()->next()) {
        if (!tok2->astOperand2())
            return empty;

        const ValueFlow::Value *index = tok2->astOperand2()->getMaxValue(conditional);
        if (!index)
            return empty;
        if (indexvarid == 0U)
            indexvarid = index->varId;
        if (index->varId > 0 && indexvarid != index->varId)
            return empty;
        if (index->intvalue < 0)
            return empty;
        indexes.push_back(*index);
    }

    return indexes;
}


void CheckBufferOverrun::valueFlowCheckArrayIndex(const Token * const tok, const ArrayInfo &arrayInfo)
{
    // Declaration in global scope or namespace?
    if (tok->scope()->type == Scope::eGlobal || tok->scope()->type == Scope::eNamespace)
        return;
    /*
         {
            const Token *parent = tok->astParent();
            while (Token::Match(parent, "%name%|::|*|&"))
                parent = parent->astParent();
            if (parent && !Token::simpleMatch(parent, "="))
                return;
        }
    */
    const bool printInconclusive = _settings->inconclusive;
    // Taking address?
    const bool addressOf = isAddressOf(tok);

    // Look for errors first
    for (int warn = 0; warn == 0 || warn == 1; ++warn) {
        // Negative index..
        for (const Token *tok2 = tok; tok2 && tok2->str() == "["; tok2 = tok2->link()->next()) {
            const Token *index = tok2->astOperand2();
            if (!index)
                continue;
            const ValueFlow::Value *value = index->getValueLE(-1LL,_settings);
            if (value)
                negativeIndexError(index, *value);
        }

        // Index out of bounds..
        const std::vector<ValueFlow::Value> indexes(valueFlowGetArrayIndexes(tok, warn==1, arrayInfo.num().size()));
        if (indexes.size() != arrayInfo.num().size())
            continue;

        // Check if the indexes point outside the whole array..
        // char a[10][10];
        // a[0][20]  <-- ok.
        // a[9][20]  <-- error.

        // total number of elements of array..
        const MathLib::bigint totalElements = arrayInfo.numberOfElements();

        // total index..
        const MathLib::bigint totalIndex = arrayInfo.totalIndex(indexes);

        // totalElements <= 0 => Unknown size
        if (totalElements <= 0)
            continue;

        if (addressOf && totalIndex == totalElements)
            continue;

        // Is totalIndex in bounds?
        if (totalIndex >= totalElements) {
            arrayIndexOutOfBoundsError(tok, arrayInfo, indexes);
            break;
        }

        // Is any array index out of bounds?
        if (printInconclusive) {
            // check each index for overflow
            for (std::size_t i = 0; i < indexes.size(); ++i) {
                if (indexes[i].intvalue >= arrayInfo.num(i)) {
                    // The access is still within the memory range for the array
                    // so it may be intentional.
                    arrayIndexOutOfBoundsError(tok, arrayInfo, indexes);
                    break; // only warn about the first one
                }
            }
        }
    }
}



void CheckBufferOverrun::checkScope(const Token *tok, const ArrayInfo &arrayInfo)
{
    assert(tok->previous() != nullptr);
    const MathLib::biguint total_size = arrayInfo.num(0) * arrayInfo.element_size();

    const unsigned int declarationId = arrayInfo.declarationId();

    const bool printPortability = _settings->isEnabled("portability");
    const bool printWarning = _settings->isEnabled("warning");
    const bool printInconclusive = _settings->inconclusive;

    bool reassigned = false;

    for (const Token* const end = tok->scope()->classEnd; tok != end; tok = tok->next()) {
        if (reassigned && tok->str() == ";")
            break;

        if (tok->varId() == declarationId) {
            if (tok->strAt(1) == "=") {
                reassigned = true;
            }

            else if (tok->strAt(1) == "[") {
                valueFlowCheckArrayIndex(tok->next(), arrayInfo);
            }

            else if (printPortability && !tok->isCast() && tok->astParent() && tok->astParent()->str() == "+") {
                // undefined behaviour: result of pointer arithmetic is out of bounds
                const Token *index;
                if (tok == tok->astParent()->astOperand1())
                    index = tok->astParent()->astOperand2();
                else
                    index = tok->astParent()->astOperand1();
                if (index) {
                    const ValueFlow::Value *value = index->getValueGE(arrayInfo.num(0) + 1U, _settings);
                    if (!value)
                        value = index->getValueLE(-1, _settings);
                    if (value)
                        pointerOutOfBoundsError(tok->astParent(), index, value->intvalue);
                }
            }

            else if (printPortability && tok->astParent() && tok->astParent()->str() == "-") {
                const Variable *var = _tokenizer->getSymbolDatabase()->getVariableFromVarId(declarationId);
                if (var && var->isArray()) {
                    const Token *index = tok->astParent()->astOperand2();
                    const ValueFlow::Value *value = index ? index->getValueGE(1,_settings) : nullptr;
                    if (index && !value)
                        value = index->getValueLE(-1 - arrayInfo.num(0), _settings);
                    if (value)
                        pointerOutOfBoundsError(tok->astParent(), index, value->intvalue);
                }
            }
        }

        else if (!tok->scope()->isExecutable()) // No executable code outside of executable scope - continue to increase performance
            continue;

        else if (Token::Match(tok, "%name% (")) {
            // Check function call..
            checkFunctionCall(tok, arrayInfo, std::list<const Token*>());

            if (printWarning && printInconclusive && Token::Match(tok, "strncpy|memcpy|memmove ( %varid% , %str% , %num% )", declarationId)) {
                if (Token::getStrLength(tok->tokAt(4)) >= total_size) {
                    const MathLib::biguint num = MathLib::toULongNumber(tok->strAt(6));
                    if (total_size == num)
                        bufferNotZeroTerminatedError(tok, tok->strAt(2), tok->str());
                }
            }

            if (printWarning && Token::Match(tok, "strncpy|strncat ( %varid% ,", declarationId) && Token::Match(tok->linkAt(1)->tokAt(-2), ", %num% )")) {
                const Token* param3 = tok->linkAt(1)->previous();

                // check for strncpy which is not terminated
                if (tok->str() == "strncpy") {
                    // strncpy takes entire variable length as input size
                    const MathLib::biguint num = MathLib::toULongNumber(param3->str());

                    // this is currently 'inconclusive'. See TestBufferOverrun::terminateStrncpy3
                    if (printInconclusive && num >= total_size) {
                        const Token *tok2 = tok->next()->link()->next();
                        for (; tok2; tok2 = tok2->next()) {
                            const Token* tok3 = tok->tokAt(2);
                            if (tok2->varId() == tok3->varId()) {
                                if (!Token::Match(tok2, "%varid% [ %any% ] = 0 ;", tok3->varId())) {
                                    terminateStrncpyError(tok, tok3->str());
                                }

                                break;
                            }
                        }
                    }
                }

                // Dangerous usage of strncat..
                else if (tok->str() == "strncat") {
                    const MathLib::biguint n = MathLib::toULongNumber(param3->str());
                    if (n >= total_size)
                        strncatUsageError(tok);
                }

                // Dangerous usage of strncpy + strncat..
                if (Token::Match(param3->tokAt(2), "; strncat ( %varid% ,", declarationId) && Token::Match(param3->linkAt(4)->tokAt(-2), ", %num% )")) {
                    const MathLib::biguint n = MathLib::toULongNumber(param3->str()) + MathLib::toULongNumber(param3->linkAt(4)->strAt(-1));
                    if (n > total_size)
                        strncatUsageError(param3->tokAt(3));
                }
            }

            // Writing data into array..
            if (total_size > 0 && Token::Match(tok, "strcpy|strcat ( %varid% , %str% )", declarationId)) {
                const std::size_t len = Token::getStrLength(tok->tokAt(4));
                if (len >= total_size) {
                    bufferOverrunError(tok, arrayInfo.varname());
                    continue;
                }
            }

            // Detect few strcat() calls
            if (total_size > 0) {
                MathLib::biguint charactersAppend = 0;
                const Token *tok2 = tok;

                while (Token::Match(tok2, "strcat ( %varid% , %str% ) ;", declarationId)) {
                    charactersAppend += Token::getStrLength(tok2->tokAt(4));
                    if (charactersAppend >= total_size) {
                        bufferOverrunError(tok2, arrayInfo.varname());
                        break;
                    }
                    tok2 = tok2->tokAt(7);
                }
            }
        }
    }
}

//---------------------------------------------------------------------------
// Negative size in array declarations
//---------------------------------------------------------------------------

static bool isVLAIndex(const Token *index)
{
    std::stack<const Token *> tokens;
    tokens.push(index);
    while (!tokens.empty()) {
        const Token *tok = tokens.top();
        tokens.pop();
        if (!tok)
            continue;
        if (tok->varId() != 0U)
            return true;
        if (tok->str() == "?") {
            // this is a VLA index if both expressions around the ":" is VLA index
            if (tok->astOperand2() &&
                tok->astOperand2()->str() == ":" &&
                isVLAIndex(tok->astOperand2()->astOperand1()) &&
                isVLAIndex(tok->astOperand2()->astOperand2()))
                return true;
            continue;
        }
        tokens.push(tok->astOperand1());
        tokens.push(tok->astOperand2());
    }
    return false;
}

void CheckBufferOverrun::negativeArraySize()
{
    const SymbolDatabase* symbolDatabase = _tokenizer->getSymbolDatabase();
    for (unsigned int i = 1; i <= _tokenizer->varIdCount(); i++) {
        const Variable * const var = symbolDatabase->getVariableFromVarId(i);
        if (!var || !var->isArray())
            continue;
        const Token * const nameToken = var->nameToken();
        if (!Token::Match(nameToken, "%var% [") || !nameToken->next()->astOperand2())
            continue;
        const ValueFlow::Value *sz = nameToken->next()->astOperand2()->getValueLE(-1,_settings);
        // don't warn about constant negative index because that is a compiler error
        if (sz && isVLAIndex(nameToken->next()->astOperand2()))
            negativeArraySizeError(nameToken);
    }
}

void CheckBufferOverrun::negativeArraySizeError(const Token *tok)
{
    reportError(tok, Severity::error, "negativeArraySize",
                "Declaration of array '" + (tok ? tok->str() : std::string()) + "' with negative size is undefined behaviour");
}

//---------------------------------------------------------------------------
// Checking member variables of structs.
//---------------------------------------------------------------------------
bool CheckBufferOverrun::isArrayOfStruct(const Token* tok, int &position)
{
    if (Token::Match(tok->next(), "%name% [ %num% ]")) {
        tok = tok->tokAt(4);
        int i = 1;
        for (;;) {
            if (Token::Match(tok->next(), "[ %num% ]")) {
                i++;
                tok = tok->tokAt(4);
            } else
                break;
        }
        if (Token::simpleMatch(tok->next(),";")) {
            position = i;
            return true;
        }
    }
    return false;
}

//---------------------------------------------------------------------------
// Checking local variables in a scope
//---------------------------------------------------------------------------

void CheckBufferOverrun::checkGlobalAndLocalVariable()
{
    // check string literals
    for (const Token *tok = _tokenizer->tokens(); tok; tok = tok->next()) {
        if (Token::Match(tok, "%str% [") && tok->next()->astOperand2()) {
            const std::size_t size = Token::getStrSize(tok);
            const ValueFlow::Value *value = tok->next()->astOperand2()->getMaxValue(false);
            if (value && value->intvalue >= (isAddressOf(tok) ? size + 1U : size))
                bufferOverrunError(tok, tok->str());
        }

        if (Token::Match(tok, "%var% [") && tok->next()->astOperand2() && tok->variable() && tok->variable()->isPointer()) {
            const ValueFlow::Value *value = tok->next()->astOperand2()->getMaxValue(false);
            if (!value)
                continue;

            for (std::list<ValueFlow::Value>::const_iterator it = tok->values.begin(); it != tok->values.end(); ++it) {
                if (!it->tokvalue)
                    continue;
                const Variable *var = it->tokvalue->variable();
                if (var && var->isArray()) {
                    if (astCanonicalType(tok) != astCanonicalType(it->tokvalue))
                        continue;

                    const ArrayInfo arrayInfo(var, _tokenizer, &_settings->library);
                    const MathLib::bigint elements = arrayInfo.numberOfElements();
                    if (elements <= 0) // unknown size
                        continue;

                    const std::vector<ValueFlow::Value> indexes(valueFlowGetArrayIndexes(tok->next(), false, var->dimensions().size()));
                    if (indexes.size() != var->dimensions().size())
                        continue;

                    const MathLib::bigint index = arrayInfo.totalIndex(indexes);
                    if (index < (isAddressOf(tok) ? elements + 1U : elements))
                        continue;

                    std::list<const Token *> callstack;
                    callstack.push_back(it->tokvalue);
                    callstack.push_back(tok);

                    std::vector<MathLib::bigint> indexes2;
                    for (unsigned int i = 0; i < indexes.size(); ++i)
                        indexes2.push_back(indexes[i].intvalue);

                    arrayIndexOutOfBoundsError(callstack, arrayInfo, indexes2);
                }
            }
        }
    }

    // check all known fixed size arrays first by just looking them up
    const SymbolDatabase* symbolDatabase = _tokenizer->getSymbolDatabase();
    for (unsigned int i = 1; i <= _tokenizer->varIdCount(); i++) {
        const Variable * const var = symbolDatabase->getVariableFromVarId(i);
        if (var && var->isArray() && var->dimension(0) > 0) {
            const Token *tok = var->nameToken();
            do {
                if (tok->str() == "{") {
                    if (Token::simpleMatch(tok->previous(), "= {"))
                        tok = tok->link();
                    else
                        break;
                }
                tok = tok->next();
            } while (tok && tok->str() != ";");
            if (!tok)
                break;
            if (tok->str() == "{")
                tok = tok->next();
            const ArrayInfo arrayInfo(var, _tokenizer, &_settings->library, i);
            checkScope(tok, arrayInfo);
        }
    }

    // find all dynamically allocated arrays next
    const std::size_t functions = symbolDatabase->functionScopes.size();
    for (std::size_t i = 0; i < functions; ++i) {
        const Scope * scope = symbolDatabase->functionScopes[i];

        for (const Token *tok = scope->classStart; tok != scope->classEnd; tok = tok->next()) {
            // if the previous token exists, it must be either a variable name or "[;{}]"
            if (tok->previous() && !tok->previous()->isName() && !Token::Match(tok->previous(), "[;{}]"))
                continue;

            // size : Max array index
            MathLib::bigint size = 0;

            // type : The type of a array element
            std::string type;

            // varid : The variable id for the array
            const Variable *var = nullptr;

            // nextTok : number of tokens used in variable declaration - used to skip to next statement.
            int nextTok = 0;

            _errorLogger->reportProgress(_tokenizer->list.getSourceFilePath(),
                                         "Check (BufferOverrun::checkGlobalAndLocalVariable)",
                                         tok->progressValue());

            if (_tokenizer->isCPP() && Token::Match(tok, "[*;{}] %var% = new %type% [ %num% ]")) {
                size = MathLib::toLongNumber(tok->strAt(6));
                type = tok->strAt(4);
                var = tok->next()->variable();
                nextTok = 8;
                if (size < 0) {
                    negativeMemoryAllocationSizeError(tok->next()->next());
                }
            } else if (_tokenizer->isCPP() && Token::Match(tok, "[*;{}] %var% = new %type% ( %num%|%name% )")) {
                size = 1;
                type = tok->strAt(4);
                var = tok->next()->variable();
                nextTok = 8;
            } else if (Token::Match(tok, "[*;{}] %var% = malloc|alloca ( %num% ) ;")) {
                size = MathLib::toLongNumber(tok->strAt(5));
                type = "char";   // minimum type, typesize=1
                var = tok->next()->variable();
                nextTok = 7;

                if (size < 0) {
                    negativeMemoryAllocationSizeError(tok->next()->next());
                }

                /** @todo false negatives: this may be too conservative */
                if (!var || !var->isPointer() || var->typeStartToken()->next() != var->typeEndToken())
                    continue;

                // get type of variable
                type = var->typeStartToken()->str();

                // malloc() gets count of bytes and not count of
                // elements, so we should calculate count of elements
                // manually
                const unsigned int sizeOfType = _tokenizer->sizeOfType(var->typeStartToken());
                if (sizeOfType > 0) {
                    size /= static_cast<int>(sizeOfType);
                }
                if (size < 0) {
                    negativeMemoryAllocationSizeError(tok->next()->next());
                }
            } else {
                continue;
            }

            if (var == 0)
                continue;

            Token sizeTok(0);
            sizeTok.str(type);
            const MathLib::bigint totalSize = size * static_cast<int>(_tokenizer->sizeOfType(&sizeTok));
            if (totalSize == 0)
                continue;

            std::vector<std::string> v;
            ArrayInfo temp(var->declarationId(), tok->next()->str(), totalSize / size, size);
            checkScope(tok->tokAt(nextTok), v, temp);
        }
    }
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// Checking member variables of structs.
//---------------------------------------------------------------------------

void CheckBufferOverrun::checkStructVariable()
{
    const SymbolDatabase * symbolDatabase = _tokenizer->getSymbolDatabase();

    // find every class and struct
    const std::size_t classes = symbolDatabase->classAndStructScopes.size();
    for (std::size_t i = 0; i < classes; ++i) {
        const Scope * scope = symbolDatabase->classAndStructScopes[i];

        // check all variables to see if they are arrays
        std::list<Variable>::const_iterator var;
        for (var = scope->varlist.begin(); var != scope->varlist.end(); ++var) {
            if (var->isArray()) {
                // create ArrayInfo from the array variable
                ArrayInfo arrayInfo(&*var, _tokenizer, &_settings->library);

                // find every function
                const std::size_t functions = symbolDatabase->functionScopes.size();
                for (std::size_t j = 0; j < functions; ++j) {
                    const Scope * func_scope = symbolDatabase->functionScopes[j];

                    // If struct is declared in a function then check
                    // if scope_func matches
                    if (scope->nestedIn->type == Scope::eFunction &&
                        scope->nestedIn != func_scope) {
                        continue;
                    }

                    // check for member variables
                    if (func_scope->functionOf == scope) {
                        // only check non-empty function
                        if (func_scope->classStart->next() != func_scope->classEnd) {
                            // start checking after the {
                            const Token *tok = func_scope->classStart->next();
                            checkScope(tok, arrayInfo);
                        }
                    }

                    // skip inner scopes..
                    /** @todo false negatives: handle inner scopes someday */
                    if (scope->nestedIn->isClassOrStruct())
                        continue;

                    std::vector<std::string> varname;
                    varname.push_back("");
                    varname.push_back(arrayInfo.varname());

                    // search the function and it's parameters
                    for (const Token *tok3 = func_scope->classDef; tok3 && tok3 != func_scope->classEnd; tok3 = tok3->next()) {
                        // search for the class/struct name
                        if (tok3->str() != scope->className)
                            continue;

                        // find all array variables
                        int posOfSemicolon = -1;

                        // Declare variable: Fred fred1;
                        if (Token::Match(tok3->next(), "%var% ;"))
                            varname[0] = tok3->strAt(1);

                        else if (isArrayOfStruct(tok3,posOfSemicolon)) {
                            varname[0] = tok3->strAt(1);

                            int pos = 2;
                            for (int k = 0 ; k < posOfSemicolon; k++) {
                                for (int index = pos; index < (pos + 3); index++)
                                    tok3->strAt(index);
                                pos += 3;
                            }
                        }

                        // Declare pointer or reference: Fred *fred1
                        else if (Token::Match(tok3->next(), "*|& %var% [,);=]"))
                            varname[0] = tok3->strAt(2);

                        else
                            continue;

                        // check for variable sized structure
                        if (scope->type == Scope::eStruct && var->isPublic()) {
                            // last member of a struct with array size of 0 or 1 could be a variable sized structure
                            if (var->dimensions().size() == 1 && var->dimension(0) < 2 &&
                                var->index() == (scope->varlist.size() - 1)) {
                                // dynamically allocated so could be variable sized structure
                                if (tok3->next()->str() == "*") {
                                    // check for allocation
                                    if ((Token::Match(tok3->tokAt(3), "; %name% = malloc ( %num% ) ;") ||
                                         (Token::Match(tok3->tokAt(3), "; %name% = (") &&
                                          Token::Match(tok3->linkAt(6), ") malloc ( %num% ) ;"))) &&
                                        (tok3->strAt(4) == tok3->strAt(2))) {
                                        MathLib::bigint size;

                                        // find size of allocation
                                        if (tok3->strAt(3) == "(") // has cast
                                            size = MathLib::toLongNumber(tok3->linkAt(6)->strAt(3));
                                        else
                                            size = MathLib::toLongNumber(tok3->strAt(8));

                                        // We don't calculate the size of a structure even when we know
                                        // the size of the members.  We just assign a length of 100 for
                                        // any struct.  If the size is less than 100, we assume the
                                        // programmer knew the size and specified it rather than using
                                        // sizeof(struct). If the size is greater than 100, we assume
                                        // the programmer specified the size as sizeof(struct) + number.
                                        // Either way, this is just a guess and could be wrong.  The
                                        // information to make the right decision has been simplified
                                        // away by the time we get here.
                                        if (size != 100) { // magic number for size of struct
                                            // check if a real size was specified and give up
                                            // malloc(10) rather than malloc(sizeof(struct))
                                            if (size < 100 || arrayInfo.element_size() == 0)
                                                continue;

                                            // calculate real array size based on allocated size
                                            MathLib::bigint elements = (size - 100) / arrayInfo.element_size();
                                            arrayInfo.num(0, arrayInfo.num(0) + elements);
                                        }
                                    }

                                    // size unknown so assume it is a variable sized structure
                                    else
                                        continue;
                                }
                            }
                        }

                        // Goto end of statement.
                        const Token *checkTok = nullptr;
                        while (tok3 && tok3 != func_scope->classEnd) {
                            // End of statement.
                            if (tok3->str() == ";") {
                                checkTok = tok3;
                                break;
                            }

                            // End of function declaration..
                            if (Token::simpleMatch(tok3, ") ;"))
                                break;

                            // Function implementation..
                            if (Token::simpleMatch(tok3, ") {")) {
                                checkTok = tok3->tokAt(2);
                                break;
                            }

                            tok3 = tok3->next();
                        }

                        if (!tok3)
                            break;

                        if (!checkTok)
                            continue;

                        // Check variable usage..
                        ArrayInfo temp = arrayInfo;
                        temp.declarationId(0); // do variable lookup by variable and member names rather than varid
                        std::string varnames; // use class and member name for messages
                        for (std::size_t k = 0; k < varname.size(); ++k)
                            varnames += (k == 0 ? "" : ".") + varname[k];

                        temp.varname(varnames);
                        checkScope(checkTok, varname, temp);
                    }
                }
            }
        }
    }
}
//---------------------------------------------------------------------------

void CheckBufferOverrun::bufferOverrun()
{
    checkGlobalAndLocalVariable();
    checkStructVariable();
    checkBufferAllocatedWithStrlen();
    checkStringArgument();
    checkInsecureCmdLineArgs();
}
//---------------------------------------------------------------------------

void CheckBufferOverrun::bufferOverrun2()
{
    // singlepass checking using ast, symboldatabase and valueflow
    for (const Token *tok = _tokenizer->tokens(); tok; tok = tok->next()) {
        // Array index
        if (!Token::Match(tok, "%name% ["))
            continue;

        // TODO: what to do about negative index..
        const Token *index = tok->next()->astOperand2();
        if (index && index->getValueLE(-1LL,_settings))
            continue;

        // Set full varname..
        std::string varname;
        if (tok->astParent() && tok->astParent()->str() == ".") {
            const Token *parent = tok->astParent();
            while (parent->astParent() && parent->astParent()->str() == ".")
                parent = parent->astParent();
            varname = parent->expressionString();
        } else
            varname = tok->str();


        const Token * const strtoken = tok->getValueTokenMinStrSize();
        if (strtoken) {
            ArrayInfo arrayInfo(tok->varId(), varname, 1U, Token::getStrSize(strtoken));
            valueFlowCheckArrayIndex(tok->next(), arrayInfo);
        }

        else {
            const Variable * const var = tok->variable();
            if (!var || var->nameToken() == tok || !var->isArray())
                continue;

            // TODO: last array in struct..
            if (var->dimension(0) <= 1 && Token::simpleMatch(var->nameToken()->linkAt(1),"] ; }"))
                continue;

            ArrayInfo arrayInfo(var, _tokenizer, &_settings->library);
            arrayInfo.varname(varname);

            valueFlowCheckArrayIndex(tok->next(), arrayInfo);
        }
    }
}
//---------------------------------------------------------------------------

MathLib::biguint CheckBufferOverrun::countSprintfLength(const std::string &input_string, const std::list<const Token*> &parameters)
{
    bool percentCharFound = false;
    std::size_t input_string_size = 1;
    bool handleNextParameter = false;
    std::string digits_string;
    bool i_d_x_f_found = false;
    std::list<const Token*>::const_iterator paramIter = parameters.begin();
    std::size_t parameterLength = 0;
    for (std::string::size_type i = 0; i < input_string.length(); ++i) {
        if (input_string[i] == '\\') {
            if (i < input_string.length() - 1 && input_string[i + 1] == '0')
                break;

            ++input_string_size;
            ++i;
            continue;
        }

        if (percentCharFound) {
            switch (input_string[i]) {
            case 'f':
            case 'x':
            case 'X':
            case 'i':
                i_d_x_f_found = true;
                handleNextParameter = true;
                break;
            case 'c':
            case 'e':
            case 'E':
            case 'g':
            case 'o':
            case 'u':
            case 'p':
            case 'n':
                handleNextParameter = true;
                break;
            case 'd':
                i_d_x_f_found = true;
                if (paramIter != parameters.end() && *paramIter && (*paramIter)->tokType() != Token::eString)
                    parameterLength = (*paramIter)->str().length();

                handleNextParameter = true;
                break;
            case 's':
                if (paramIter != parameters.end() && *paramIter && (*paramIter)->tokType() == Token::eString)
                    parameterLength = Token::getStrLength(*paramIter);

                handleNextParameter = true;
                break;
            }
        }

        if (input_string[i] == '%')
            percentCharFound = !percentCharFound;
        else if (percentCharFound) {
            digits_string.append(1, input_string[i]);
        }

        if (!percentCharFound)
            input_string_size++;

        if (handleNextParameter) {
            unsigned int tempDigits = static_cast<unsigned int>(std::abs(std::atoi(digits_string.c_str())));
            if (i_d_x_f_found)
                tempDigits = std::max(static_cast<unsigned int>(tempDigits), 1U);

            if (digits_string.find('.') != std::string::npos) {
                const std::string endStr = digits_string.substr(digits_string.find('.') + 1);
                const unsigned int maxLen = std::max(static_cast<unsigned int>(std::abs(std::atoi(endStr.c_str()))), 1U);

                if (input_string[i] == 's') {
                    // For strings, the length after the dot "%.2s" will limit
                    // the length of the string.
                    if (parameterLength > maxLen)
                        parameterLength = maxLen;
                } else {
                    // For integers, the length after the dot "%.2d" can
                    // increase required length
                    if (tempDigits < maxLen)
                        tempDigits = maxLen;
                }
            }

            if (tempDigits < parameterLength)
                input_string_size += parameterLength;
            else
                input_string_size += tempDigits;

            parameterLength = 0;
            digits_string = "";
            i_d_x_f_found = false;
            percentCharFound = false;
            handleNextParameter = false;
            if (paramIter != parameters.end())
                ++paramIter;
        }
    }

    return input_string_size;
}



//---------------------------------------------------------------------------
// Checking for allocating insufficient memory for copying a string by
// allocating only strlen(src) bytes instead of strlen(src) + 1 bytes (one
// extra for the terminating null character).
// Example:
//   char *b = malloc(strlen(a));   // Should be malloc(strlen(a) + 1);
//   strcpy(b, a);                  // <== Buffer overrun
//---------------------------------------------------------------------------
void CheckBufferOverrun::checkBufferAllocatedWithStrlen()
{
    const SymbolDatabase* const symbolDatabase = _tokenizer->getSymbolDatabase();

    const std::size_t functions = symbolDatabase->functionScopes.size();
    for (std::size_t i = 0; i < functions; ++i) {
        const Scope * scope = symbolDatabase->functionScopes[i];
        for (const Token *tok = scope->classStart->next(); tok && tok != scope->classEnd; tok = tok->next()) {
            unsigned int dstVarId;
            unsigned int srcVarId;

            // Look for allocation of a buffer based on the size of a string
            if (Token::Match(tok, "%var% = malloc|g_malloc|g_try_malloc ( strlen ( %name% ) )")) {
                dstVarId = tok->varId();
                srcVarId = tok->tokAt(6)->varId();
                tok      = tok->tokAt(8);
            } else if (_tokenizer->isCPP() && Token::Match(tok, "%var% = new char [ strlen ( %name% ) ]")) {
                dstVarId = tok->varId();
                srcVarId = tok->tokAt(7)->varId();
                tok      = tok->tokAt(9);
            } else if (Token::Match(tok, "%var% = realloc|g_realloc|g_try_realloc ( %name% , strlen ( %name% ) )")) {
                dstVarId = tok->varId();
                srcVarId = tok->tokAt(8)->varId();
                tok      = tok->tokAt(10);
            } else
                continue;

            // To avoid false positives and added complexity, we will only look for
            // improper usage of the buffer within the block that it was allocated
            for (const Token* const end = tok->scope()->classEnd; tok && tok->next() && tok != end; tok = tok->next()) {
                // If the buffers are modified, we can't be sure of their sizes
                if (tok->varId() == srcVarId || tok->varId() == dstVarId)
                    break;

                if (Token::Match(tok, "strcpy ( %varid% , %var% )", dstVarId) &&
                    tok->tokAt(4)->varId() == srcVarId) {
                    bufferOverrunError(tok);
                }
            }
            if (!tok)
                return;
        }
    }
}

//---------------------------------------------------------------------------
// memcpy(temp, "hello world", 50);
//---------------------------------------------------------------------------
void CheckBufferOverrun::checkStringArgument()
{
    const SymbolDatabase* const symbolDatabase = _tokenizer->getSymbolDatabase();
    const std::size_t functions = symbolDatabase->functionScopes.size();
    for (std::size_t functionIndex = 0; functionIndex < functions; ++functionIndex) {
        const Scope * const scope = symbolDatabase->functionScopes[functionIndex];
        for (const Token *tok = scope->classStart; tok != scope->classEnd; tok = tok->next()) {
            if (!Token::Match(tok, "%name% (") || !_settings->library.hasminsize(tok->str()))
                continue;

            unsigned int argnr = 1;
            for (const Token *argtok = tok->tokAt(2); argtok; argtok = argtok->nextArgument(), argnr++) {
                if (!Token::Match(argtok, "%name%|%str% ,|)"))
                    continue;
                if (argtok->variable() && !argtok->variable()->isPointer())
                    continue;
                const Token *strtoken = argtok->getValueTokenMinStrSize();
                if (!strtoken)
                    continue;
                const std::list<Library::ArgumentChecks::MinSize> *minsizes = _settings->library.argminsizes(tok, argnr);
                if (!minsizes)
                    continue;
                if (checkMinSizes(*minsizes, tok, Token::getStrSize(strtoken), nullptr, _settings))
                    bufferOverrunError(argtok);
            }
        }
    }
}

//---------------------------------------------------------------------------
// Checking for buffer overflow caused by copying command line arguments
// into fixed-sized buffers without checking to make sure that the command
// line arguments will not overflow the buffer.
//
// int main(int argc, char* argv[])
// {
//   char prog[10];
//   strcpy(prog, argv[0]);      <-- Possible buffer overrun
// }
//---------------------------------------------------------------------------
void CheckBufferOverrun::checkInsecureCmdLineArgs()
{
    const SymbolDatabase* const symbolDatabase = _tokenizer->getSymbolDatabase();

    const std::size_t functions = symbolDatabase->functionScopes.size();
    for (std::size_t i = 0; i < functions; ++i) {
        const Function * function = symbolDatabase->functionScopes[i]->function;
        if (function) {
            const Token* tok = function->token;

            // Get the name of the argv variable
            unsigned int varid = 0;
            if (Token::Match(tok, "main ( int %var% , char * %var% [ ] ,|)")) {
                varid = tok->tokAt(7)->varId();

            } else if (Token::Match(tok, "main ( int %var% , char * * %var% ,|)")) {
                varid = tok->tokAt(8)->varId();
            } else
                continue;

            // Jump to the opening curly brace
            tok = symbolDatabase->functionScopes[i]->classStart;

            // Search within main() for possible buffer overruns involving argv
            for (const Token* end = tok->link(); tok != end; tok = tok->next()) {
                // If argv is modified or tested, its size may be being limited properly
                if (tok->varId() == varid)
                    break;

                // Match common patterns that can result in a buffer overrun
                // e.g. strcpy(buffer, argv[0])
                if (Token::Match(tok, "strcpy|strcat ( %name% , * %varid%", varid) ||
                    Token::Match(tok, "strcpy|strcat ( %name% , %varid% [", varid)) {
                    cmdLineArgsError(tok);
                    tok = tok->linkAt(1);
                }
            }
        }
    }
}
//---------------------------------------------------------------------------


void CheckBufferOverrun::negativeIndexError(const Token *tok, MathLib::bigint index)
{
    std::ostringstream ostr;
    ostr << "Array index " << index << " is out of bounds.";
    reportError(tok, Severity::error, "negativeIndex", ostr.str());
}

void CheckBufferOverrun::negativeIndexError(const Token *tok, const ValueFlow::Value &index)
{
    std::ostringstream ostr;
    ostr << "Array index " << index.intvalue << " is out of bounds.";
    if (index.condition)
        ostr << " Otherwise there is useless condition at line " << index.condition->linenr() << ".";
    reportError(tok, index.condition ? Severity::warning : Severity::error, "negativeIndex", ostr.str(), 0U, index.inconclusive);
}

CheckBufferOverrun::ArrayInfo::ArrayInfo()
    : _element_size(0), _declarationId(0)
{
}

CheckBufferOverrun::ArrayInfo::ArrayInfo(const Variable *var, const Tokenizer *tokenizer, const Library *library, const unsigned int forcedeclid)
    : _varname(var->name()), _declarationId((forcedeclid == 0U) ? var->declarationId() : forcedeclid)
{
    for (std::size_t i = 0; i < var->dimensions().size(); i++)
        _num.push_back(var->dimension(i));
    if (var->typeEndToken()->str() == "*")
        _element_size = tokenizer->sizeOfType(var->typeEndToken());
    else if (var->typeStartToken()->str() == "struct")
        _element_size = 100;
    else {
        _element_size = tokenizer->sizeOfType(var->typeEndToken());
        if (!_element_size) { // try libary
            const Library::PodType* podtype = library->podtype(var->typeEndToken()->str());
            if (podtype)
                _element_size = podtype->size;
        }
    }
}

/**
 * Create array info with specified data
 * The intention is that this is only a temporary solution.. all
 * checking should be based on ArrayInfo from the start and then
 * this will not be needed as the declare can be used instead.
 */
CheckBufferOverrun::ArrayInfo::ArrayInfo(unsigned int id, const std::string &name, MathLib::bigint size1, MathLib::bigint n)
    : _varname(name), _element_size(size1), _declarationId(id)
{
    _num.push_back(n);
}

CheckBufferOverrun::ArrayInfo CheckBufferOverrun::ArrayInfo::limit(MathLib::bigint value) const
{
    const MathLib::bigint uvalue = std::max(MathLib::bigint(0), value);
    MathLib::bigint n = 1;
    for (std::size_t i = 0; i < _num.size(); ++i)
        n *= _num[i];
    if (uvalue > n)
        n = uvalue;
    return ArrayInfo(_declarationId, _varname, _element_size, n - uvalue);
}

MathLib::bigint CheckBufferOverrun::ArrayInfo::numberOfElements() const
{
    if (_num.empty())
        return 0;

    // total number of elements of array..
    MathLib::bigint ret = 1;
    for (std::size_t i = 0; i < _num.size(); ++i) {
        ret *= _num[i];
    }
    return ret;
}

MathLib::bigint CheckBufferOverrun::ArrayInfo::totalIndex(const std::vector<ValueFlow::Value> &indexes) const
{
    MathLib::bigint index = 0;
    MathLib::bigint elements = 1;
    for (std::size_t i = 0; i < _num.size(); ++i) {
        const std::size_t ri = _num.size() - 1U - i;
        index += indexes[ri].intvalue * elements;
        elements *= _num[ri];
    }
    return index;
}


void CheckBufferOverrun::arrayIndexThenCheck()
{
    if (!_settings->isEnabled("style"))
        return;

    const SymbolDatabase * const symbolDatabase = _tokenizer->getSymbolDatabase();
    const std::size_t functions = symbolDatabase->functionScopes.size();
    for (std::size_t i = 0; i < functions; ++i) {
        const Scope * const scope = symbolDatabase->functionScopes[i];
        for (const Token *tok = scope->classStart; tok && tok != scope->classEnd; tok = tok->next()) {
            if (Token::Match(tok, "%name% [ %var% ]")) {
                tok = tok->tokAt(2);

                const unsigned int indexID = tok->varId();
                const std::string& indexName(tok->str());

                // skip array index..
                tok = tok->tokAt(2);
                while (tok && tok->str() == "[")
                    tok = tok->link()->next();

                // syntax error
                if (!tok)
                    return;

                // skip comparison
                if (tok->tokType() == Token::eComparisonOp)
                    tok = tok->tokAt(2);

                if (!tok)
                    break;
                // skip close parentheses
                if (tok->str() == ")")
                    tok = tok->next();

                // check if array index is ok
                // statement can be closed in parentheses, so "(| " is using
                if (Token::Match(tok, "&& (| %varid% <|<=", indexID))
                    arrayIndexThenCheckError(tok, indexName);
                else if (Token::Match(tok, "&& (| %any% >|>= %varid% !!+", indexID))
                    arrayIndexThenCheckError(tok, indexName);
            }
        }
    }
}

void CheckBufferOverrun::arrayIndexThenCheckError(const Token *tok, const std::string &indexName)
{
    reportError(tok, Severity::style, "arrayIndexThenCheck",
                "Array index '" + indexName + "' is used before limits check.\n"
                "Defensive programming: The variable '" + indexName + "' is used as an array index before it "
                "is checked that is within limits. This can mean that the array might be accessed out of bounds. "
                "Reorder conditions such as '(a[i] && i < 10)' to '(i < 10 && a[i])'. That way the array will "
                "not be accessed if the index is out of limits.");
}

Check::FileInfo* CheckBufferOverrun::getFileInfo(const Tokenizer *tokenizer, const Settings *settings) const
{
    (void)settings;

    MyFileInfo *fileInfo = new MyFileInfo;

    // Array usage..
    const SymbolDatabase* const symbolDatabase = tokenizer->getSymbolDatabase();
    const std::size_t functions = symbolDatabase->functionScopes.size();
    for (std::size_t i = 0; i < functions; ++i) {
        const Scope * const scope = symbolDatabase->functionScopes[i];
        for (const Token *tok = scope->classStart; tok && tok != scope->classEnd; tok = tok->next()) {
            if (Token::Match(tok, "%var% [")          &&
                Token::Match(tok->linkAt(1), "] !![") &&
                tok->variable()                       &&
                tok->variable()->isExtern()           &&
                tok->variable()->isGlobal()           &&
                tok->next()->astOperand2()) {
                const ValueFlow::Value *value = tok->next()->astOperand2()->getMaxValue(false);
                if (value && value->intvalue > 0) {
                    const MathLib::bigint arrayIndex = value->intvalue;
                    std::map<std::string, struct MyFileInfo::ArrayUsage>::iterator it = fileInfo->arrayUsage.find(tok->str());
                    if (it != fileInfo->arrayUsage.end() && it->second.index >= arrayIndex)
                        continue;
                    struct MyFileInfo::ArrayUsage arrayUsage;
                    arrayUsage.index = arrayIndex;
                    arrayUsage.fileName = tokenizer->list.file(tok);
                    arrayUsage.linenr = tok->linenr();
                    fileInfo->arrayUsage[tok->str()] = arrayUsage;
                }
            }
        }
    }

    // Arrays..
    const std::list<Variable> &varlist = symbolDatabase->scopeList.front().varlist;
    for (std::list<Variable>::const_iterator it = varlist.begin(); it != varlist.end(); ++it) {
        const Variable &var = *it;
        if (!var.isStatic() && var.isArray() && var.dimensions().size() == 1U && var.dimension(0U) > 0U)
            fileInfo->arraySize[var.name()] = var.dimension(0U);
    }

    return fileInfo;
}

void CheckBufferOverrun::analyseWholeProgram(const std::list<Check::FileInfo*> &fileInfo, const Settings&, ErrorLogger &errorLogger)
{
    // Merge all fileInfo
    MyFileInfo all;
    for (std::list<Check::FileInfo*>::const_iterator it = fileInfo.begin(); it != fileInfo.end(); ++it) {
        const MyFileInfo *fi = dynamic_cast<MyFileInfo*>(*it);
        if (!fi)
            continue;

        // merge array usage
        for (std::map<std::string, struct MyFileInfo::ArrayUsage>::const_iterator it2 = fi->arrayUsage.begin(); it2 != fi->arrayUsage.end(); ++it2) {
            std::map<std::string, struct MyFileInfo::ArrayUsage>::const_iterator allit = all.arrayUsage.find(it2->first);
            if (allit == all.arrayUsage.end() || it2->second.index > allit->second.index)
                all.arrayUsage[it2->first] = it2->second;
        }

        // merge array info
        for (std::map<std::string, MathLib::bigint>::const_iterator it2 = fi->arraySize.begin(); it2 != fi->arraySize.end(); ++it2) {
            std::map<std::string, MathLib::bigint>::const_iterator allit = all.arraySize.find(it2->first);
            if (allit == all.arraySize.end())
                all.arraySize[it2->first] = it2->second;
            else
                all.arraySize[it2->first] = -1;
        }
    }

    // Check buffer usage
    for (std::map<std::string, struct MyFileInfo::ArrayUsage>::const_iterator it = all.arrayUsage.begin(); it != all.arrayUsage.end(); ++it) {
        std::map<std::string, MathLib::bigint>::const_iterator sz = all.arraySize.find(it->first);
        if (sz != all.arraySize.end() && sz->second > 0 && sz->second < it->second.index) {
            ErrorLogger::ErrorMessage::FileLocation fileLoc;
            fileLoc.setfile(it->second.fileName);
            fileLoc.line = it->second.linenr;

            std::list<ErrorLogger::ErrorMessage::FileLocation> locationList;
            locationList.push_back(fileLoc);

            std::ostringstream ostr;
            ostr << "Array " << it->first << '[' << sz->second << "] accessed at index " << it->second.index << " which is out of bounds";

            const ErrorLogger::ErrorMessage errmsg(locationList,
                                                   Severity::error,
                                                   ostr.str(),
                                                   "arrayIndexOutOfBounds",
                                                   false);
            errorLogger.reportErr(errmsg);
        }
    }
}
