#pragma once

#include <Poco/UTF8Encoding.h>
#include <Poco/Unicode.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypeFixedString.h>
#include <Columns/ColumnString.h>
#include <Columns/ColumnArray.h>
#include <Columns/ColumnFixedString.h>
#include <Common/hex.h>
#include <Common/Volnitsky.h>
#include <Functions/IFunction.h>
#include <Functions/FunctionHelpers.h>
#include <IO/ReadBufferFromMemory.h>
#include <IO/ReadHelpers.h>



//#include <Poco/Logger.h>
//#include <common/logger_useful.h>
//            LOG_TRACE(&Logger::get("maxim"), "run " << current_row_offset << " " << res_strings_chars.data());


/** Functions for retrieving "visit parameters".
 * Visit parameters in Yandex.Metrika are a special kind of JSONs.
 * These functions are applicable to almost any JSONs.
 * Implemented via templates from FunctionsStringSearch.h.
 *
 * Check if there is a parameter
 *         visitParamHas
 *
 * Retrieve the numeric value of the parameter
 *         visitParamExtractUInt
 *         visitParamExtractInt
 *         visitParamExtractFloat
 *         visitParamExtractBool
 *
 * Retrieve the string value of the parameter
 *         visitParamExtractString - unescape value
 *         visitParamExtractRaw
 */

namespace DB
{

struct HasParam
{
    using ResultType = UInt8;

    static UInt8 extract(const UInt8 *, const UInt8 *)
    {
        return true;
    }
};

template <typename NumericType>
struct ExtractNumericType
{
    using ResultType = NumericType;

    static ResultType extract(const UInt8 * begin, const UInt8 * end)
    {
        ReadBufferFromMemory in(begin, end - begin);

        /// Read numbers in double quotes
        if (!in.eof() && *in.position() == '"')
            ++in.position();

        ResultType x = 0;
        if (!in.eof())
        {
            if constexpr (std::is_floating_point_v<NumericType>)
                tryReadFloatText(x, in);
            else
                tryReadIntText(x, in);
        }
        return x;
    }
};

struct ExtractBool
{
    using ResultType = UInt8;

    static UInt8 extract(const UInt8 * begin, const UInt8 * end)
    {
        return begin + 4 <= end && 0 == strncmp(reinterpret_cast<const char *>(begin), "true", 4);
    }
};


struct ExtractJson
{
    static constexpr size_t bytes_on_stack = 64;
    using ExpectChars = PODArray<char, bytes_on_stack, AllocatorWithStackMemory<Allocator<false>, bytes_on_stack>>;

    static void extract(const UInt8 * pos, const UInt8 * end, ColumnString::Chars & res_data)
    {
        ExpectChars expects_end;
        UInt8 current_expect_end = 0;

        for (auto extract_begin = pos; pos != end; ++pos)
        {
            if (*pos == current_expect_end)
            {
                expects_end.pop_back();
                current_expect_end = expects_end.empty() ? 0 : expects_end.back();
            }
            else
            {
                switch (*pos)
                {
                    case '[':
                        current_expect_end = ']';
                        expects_end.push_back(current_expect_end);
                        break;
                    case '{':
                        current_expect_end = '}';
                        expects_end.push_back(current_expect_end);
                        break;
                    case '"' :
                        current_expect_end = '"';
                        expects_end.push_back(current_expect_end);
                        break;
                    case '\\':
                        /// skip backslash
                        if (pos + 1 < end && pos[1] == '"')
                            pos++;
                        break;
                    default:
                        if (!current_expect_end && (*pos == ',' || *pos == '}'))
                        {
                            auto payload_begin = extract_begin;
                            auto payload_last = pos - 1;
                            while (payload_begin < payload_last && *payload_begin == ' ')
                                ++payload_begin;
                            while (payload_last > payload_begin && *payload_last == ' ')
                                --payload_last;
                            if (payload_begin < payload_last && *payload_begin == '"' && *payload_last == '"')
                            {
                                ++payload_begin;
                                --payload_last;
                            }
                            res_data.insert(payload_begin, payload_last + 1);
                            return;
                        }
                }
            }
        }
    }
};


struct ExtractRaw
{
    static constexpr size_t bytes_on_stack = 64;
    using ExpectChars = PODArray<char, bytes_on_stack, AllocatorWithStackMemory<Allocator<false>, bytes_on_stack>>;

    static void extract(const UInt8 * pos, const UInt8 * end, ColumnString::Chars & res_data)
    {
        ExpectChars expects_end;
        UInt8 current_expect_end = 0;

        for (auto extract_begin = pos; pos != end; ++pos)
        {
            if (*pos == current_expect_end)
            {
                expects_end.pop_back();
                current_expect_end = expects_end.empty() ? 0 : expects_end.back();
            }
            else
            {
                switch (*pos)
                {
                    case '[':
                        current_expect_end = ']';
                        expects_end.push_back(current_expect_end);
                        break;
                    case '{':
                        current_expect_end = '}';
                        expects_end.push_back(current_expect_end);
                        break;
                    case '"' :
                        current_expect_end = '"';
                        expects_end.push_back(current_expect_end);
                        break;
                    case '\\':
                        /// skip backslash
                        if (pos + 1 < end && pos[1] == '"')
                            pos++;
                        break;
                    default:
                        if (!current_expect_end && (*pos == ',' || *pos == '}'))
                        {
                            res_data.insert(extract_begin, pos);
                            return;
                        }
                }
            }
        }
    }
};

struct ExtractString
{
    static void extract(const UInt8 * pos, const UInt8 * end, ColumnString::Chars & res_data)
    {
        size_t old_size = res_data.size();
        ReadBufferFromMemory in(pos, end - pos);
        if (!tryReadJSONStringInto(res_data, in))
            res_data.resize(old_size);
    }
};


/** Searches for occurrences of a field in the visit parameter and calls ParamExtractor
 * for each occurrence of the field, passing it a pointer to the part of the string,
 * where the occurrence of the field value begins.
 * ParamExtractor must parse and return the value of the desired type.
 *
 * If a field was not found or an incorrect value is associated with the field,
 * then the default value used - 0.
 */
template <typename ParamExtractor>
struct ExtractParamImpl
{
    using ResultType = typename ParamExtractor::ResultType;

    /// It is assumed that `res` is the correct size and initialized with zeros.
    static void vector_constant(const ColumnString::Chars & data, const ColumnString::Offsets & offsets,
        std::string needle,
        PaddedPODArray<ResultType> & res)
    {
        /// We are looking for a parameter simply as a substring of the form "name"
        needle = "\"" + needle + "\":";

        const UInt8 * begin = data.data();
        const UInt8 * pos = begin;
        const UInt8 * end = pos + data.size();

        /// The current index in the string array.
        size_t i = 0;

        Volnitsky searcher(needle.data(), needle.size(), end - pos);

        /// We will search for the next occurrence in all strings at once.
        while (pos < end && end != (pos = searcher.search(pos, end - pos)))
        {
            /// Let's determine which index it belongs to.
            while (begin + offsets[i] <= pos)
            {
                res[i] = 0;
                ++i;
            }

            /// We check that the entry does not pass through the boundaries of strings.
            if (pos + needle.size() < begin + offsets[i])
                res[i] = ParamExtractor::extract(pos + needle.size(), begin + offsets[i]);
            else
                res[i] = 0;

            pos = begin + offsets[i];
            ++i;
        }

        if (res.size() > i)
            memset(&res[i], 0, (res.size() - i) * sizeof(res[0]));
    }

    static void constant_constant(const std::string & data, std::string needle, ResultType & res)
    {
        needle = "\"" + needle + "\":";
        size_t pos = data.find(needle);
        if (pos == std::string::npos)
            res = 0;
        else
            res = ParamExtractor::extract(
                reinterpret_cast<const UInt8 *>(data.data() + pos + needle.size()),
                reinterpret_cast<const UInt8 *>(data.data() + data.size())
            );
    }

    template <typename... Args> static void vector_vector(Args &&...)
    {
        throw Exception("Functions 'visitParamHas' and 'visitParamExtract*' doesn't support non-constant needle argument", ErrorCodes::ILLEGAL_COLUMN);
    }

    template <typename... Args> static void constant_vector(Args &&...)
    {
        throw Exception("Functions 'visitParamHas' and 'visitParamExtract*' doesn't support non-constant needle argument", ErrorCodes::ILLEGAL_COLUMN);
    }
};


/** For the case where the type of field to extract is a string.
 */
template <typename ParamExtractor>
struct ExtractParamToStringImpl
{
    static void vector(const ColumnString::Chars & data, const ColumnString::Offsets & offsets,
                       std::string needle,
                       ColumnString::Chars & res_data, ColumnString::Offsets & res_offsets)
    {
        /// Constant 5 is taken from a function that performs a similar task FunctionsStringSearch.h::ExtractImpl
        res_data.reserve(data.size() / 5);
        res_offsets.resize(offsets.size());

        /// We are looking for a parameter simply as a substring of the form "name"
        needle = "\"" + needle + "\":";

        const UInt8 * begin = data.data();
        const UInt8 * pos = begin;
        const UInt8 * end = pos + data.size();

        /// The current index in the string array.
        size_t i = 0;

        Volnitsky searcher(needle.data(), needle.size(), end - pos);

        /// We will search for the next occurrence in all strings at once.
        while (pos < end && end != (pos = searcher.search(pos, end - pos)))
        {
            /// Determine which index it belongs to.
            while (begin + offsets[i] <= pos)
            {
                res_data.push_back(0);
                res_offsets[i] = res_data.size();
                ++i;
            }

            /// We check that the entry does not pass through the boundaries of strings.
            if (pos + needle.size() < begin + offsets[i])
                ParamExtractor::extract(pos + needle.size(), begin + offsets[i], res_data);

            pos = begin + offsets[i];

            res_data.push_back(0);
            res_offsets[i] = res_data.size();
            ++i;
        }

        while (i < res_offsets.size())
        {
            res_data.push_back(0);
            res_offsets[i] = res_data.size();
            ++i;
        }
    }
};


struct ExtractJsonWithPathSupportImpl
{
    static void vector(const ColumnString::Chars & data, const ColumnString::Offsets & offsets,
                       std::string needle,
                       ColumnString::Chars & res_data, ColumnString::Offsets & res_offsets)
    {
        std::size_t dot_pos = needle.find(".");
        std::size_t prev_pos = 0;

        if (std::string::npos == dot_pos)
        {
            ExtractParamToStringImpl<ExtractJson>::vector(
                data,
                offsets,
                needle,
                res_data,
                res_offsets);
            return;
        }

        std::shared_ptr<ColumnString::Chars> temporary_input_data;
        std::shared_ptr<ColumnString::Offsets> temporary_input_offsets;

        while (std::string::npos != dot_pos)
        {
            auto temporary_res_data = std::make_shared<ColumnString::Chars>();
            auto temporary_res_offsets = std::make_shared<ColumnString::Offsets>();
            if (0 == prev_pos)
                ExtractParamToStringImpl<ExtractJson>::vector(
                    data,
                    offsets,
                    needle.substr(prev_pos, dot_pos - prev_pos),
                    *temporary_res_data,
                    *temporary_res_offsets);
            else
                ExtractParamToStringImpl<ExtractJson>::vector(
                        *temporary_input_data,
                        *temporary_input_offsets,
                        needle.substr(prev_pos, dot_pos - prev_pos),
                        *temporary_res_data,
                        *temporary_res_offsets);

            temporary_input_data = temporary_res_data;
            temporary_input_offsets = temporary_res_offsets;

            prev_pos = dot_pos + 1;
            dot_pos = needle.find(".", prev_pos);
        }
        ExtractParamToStringImpl<ExtractJson>::vector(
                *temporary_input_data,
                *temporary_input_offsets,
                needle.substr(prev_pos, std::string::npos),
                res_data,
                res_offsets);
    }
};

template <typename Name>
class FunctionJsonsWithPathSupport : public IFunction
{
    using Pos = const char *;
    static constexpr size_t bytes_on_stack = 64;
    using ExpectChars = PODArray<char, bytes_on_stack, AllocatorWithStackMemory<Allocator<false>, bytes_on_stack>>;

public:
    static constexpr auto name = Name::name;
    static FunctionPtr create(const Context &) { return std::make_shared<FunctionJsonsWithPathSupport>(); }

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 2; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (!isStringOrFixedString(arguments[0]))
            throw Exception("Illegal type " + arguments[0]->getName() + " of first argument of function " + getName() + ". Must be String.",
                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        if (!isStringOrFixedString(arguments[1]))
            throw Exception("Illegal type " + arguments[1]->getName() + " of second argument of function " + getName() + ". Must be String.",
                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeArray>(std::make_shared<DataTypeString>());
    }

    size_t tokenizeArray(
            Pos begin,
            Pos last,
            ColumnString::Chars & res_strings_chars,
            ColumnString::Offsets & res_strings_offsets,
            ColumnString::Offset & current_dst_strings_offset)
    {
        ExpectChars expects_end;
        UInt8 current_expect_end = 0;
        size_t num_elements = 0;
        Pos pos;

        for (pos = begin; pos < last; ++pos)
        {
            if (*pos == current_expect_end)
            {
                expects_end.pop_back();
                current_expect_end = expects_end.empty() ? 0 : expects_end.back();
            }
            else
            {
                switch (*pos)
                {
                    case '[':
                        current_expect_end = ']';
                        expects_end.push_back(current_expect_end);
                        break;
                    case '{':
                        current_expect_end = '}';
                        expects_end.push_back(current_expect_end);
                        break;
                    case '"' :
                        current_expect_end = '"';
                        expects_end.push_back(current_expect_end);
                        break;
                    case '\\':
                        /// skip backslash
                        if (pos + 1 < last && pos[1] == '"')
                            pos++;
                        break;
                    default:
                        if (!current_expect_end && *pos == ',')
                        {
                            insertResultElement(begin, pos - 1, res_strings_chars, res_strings_offsets, current_dst_strings_offset);
                            ++num_elements;
                            begin = pos + 1;
                        }
                }
            }
        }

        if (pos >= begin)
        {
            insertResultElement(begin, pos, res_strings_chars, res_strings_offsets, current_dst_strings_offset);
            ++num_elements;
        }

        return num_elements;
    }

    void insertResultElement(
            Pos begin,
            Pos last,
            ColumnString::Chars & res_strings_chars,
            ColumnString::Offsets & res_strings_offsets,
            ColumnString::Offset & current_dst_strings_offset)
    {
        size_t bytes_to_copy = last - begin + 1;
        memcpySmallAllowReadWriteOverflow15(&res_strings_chars[current_dst_strings_offset], begin, bytes_to_copy);
        current_dst_strings_offset += bytes_to_copy;
        res_strings_chars[current_dst_strings_offset] = '\0';
        ++current_dst_strings_offset;
        res_strings_offsets.push_back(current_dst_strings_offset);
    }

    void parseArrays(
            const ColumnString::Chars & data,
            const ColumnString::Offsets & offsets,
            ColumnArray::Offsets & res_offsets,
            ColumnString::Chars & res_strings_chars,
            ColumnString::Offsets & res_strings_offsets)
    {
        res_offsets.reserve(offsets.size());
        res_strings_offsets.reserve(offsets.size() * 5);    /// Constant 5 - at random.
        res_strings_chars.reserve(data.size());

        ColumnString::Offset current_src_offset = 0;
        ColumnArray::Offset current_dst_offset = 0;
        ColumnString::Offset current_dst_strings_offset = 0;
        for (size_t i = 0; i < offsets.size(); ++i)
        {
            Pos begin = reinterpret_cast<Pos>(&data[current_src_offset]);
            current_src_offset = offsets[i];
            Pos end = reinterpret_cast<Pos>(&data[current_src_offset]) - 1;

            // end points to the \0 marker of the end of the string,
            // last points to the actual last char of the current record.
            Pos last = end - 1;

            while (' ' == *begin && begin < end)
                ++begin;

            while (' ' == *last && begin < last)
                --last;

            if ('[' == *begin && ']' == *last)
            {
                current_dst_offset += tokenizeArray(begin + 1, last - 1, res_strings_chars, res_strings_offsets, current_dst_strings_offset);
            }
            else
            {
                insertResultElement(begin, last, res_strings_chars, res_strings_offsets, current_dst_strings_offset);
                ++current_dst_offset;
            }
            res_offsets.push_back(current_dst_offset);
        }
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result, size_t /*input_rows_count*/) override
    {
        size_t json_arg = arguments[0];
        size_t path_arg = arguments[1];

        if (!block.getByPosition(path_arg).column->isColumnConst())
            throw Exception(getName() + " currently supports only constant expressions as second argument", ErrorCodes::ILLEGAL_COLUMN);

        const ColumnConst * needle_str =
                checkAndGetColumnConstStringOrFixedString(block.getByPosition(path_arg).column.get());
        String needle = needle_str->getValue<String>();

        const auto maybe_const = block.getByPosition(json_arg).column.get()->convertToFullColumnIfConst();
        const ColumnString * json_str = checkAndGetColumn<ColumnString>(maybe_const.get());

        if (json_str)
        {
            const ColumnString::Chars & json_chars = json_str->getChars();
            const ColumnString::Offsets & json_offsets = json_str->getOffsets();

            auto temporary_res = ColumnString::create();
            ColumnString::Chars & temporary_res_chars = temporary_res->getChars();
            ColumnArray::Offsets & temporary_res_offsets = temporary_res->getOffsets();

            ExtractJsonWithPathSupportImpl::vector(json_chars, json_offsets, needle, temporary_res_chars, temporary_res_offsets);

            auto col_res = ColumnArray::create(ColumnString::create());
            ColumnString & res_strings = typeid_cast<ColumnString &>(col_res->getData());
            ColumnArray::Offsets & res_offsets = col_res->getOffsets();
            ColumnString::Chars & res_strings_chars = res_strings.getChars();
            ColumnString::Offsets & res_strings_offsets = res_strings.getOffsets();

            parseArrays(temporary_res_chars, temporary_res_offsets, res_offsets, res_strings_chars, res_strings_offsets);

            block.getByPosition(result).column = std::move(col_res);
        }
        else
            throw Exception("Illegal columns " + block.getByPosition(0).column->getName()
                    + ", " + block.getByPosition(1).column->getName()
                    + " of arguments of function " + getName(),
                ErrorCodes::ILLEGAL_COLUMN);
    }
};


template <typename Name>
class FunctionMultiJsonWithPathSupport : public IFunction
{
    using Pos = const unsigned char *;
    static constexpr size_t bytes_on_stack = 64;
    using ExpectChars = PODArray<char, bytes_on_stack, AllocatorWithStackMemory<Allocator<false>, bytes_on_stack>>;

public:
    static constexpr auto name = Name::name;
    static FunctionPtr create(const Context &) { return std::make_shared<FunctionMultiJsonWithPathSupport>(); }

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 2; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (!isStringOrFixedString(arguments[0]))
            throw Exception("Illegal type " + arguments[0]->getName() + " of first argument of function " + getName() + ". Must be String.",
                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        if (!isStringOrFixedString(arguments[1]))
            throw Exception("Illegal type " + arguments[1]->getName() + " of second argument of function " + getName() + ". Must be String.",
                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return std::make_shared<DataTypeArray>(std::make_shared<DataTypeString>());
    }


    Pos findBeginLast(Pos pos, Pos end, Pos & payload_begin, Pos & payload_last)
    {
        ExpectChars expects_end;
        UInt8 current_expect_end = 0;

        for (auto extract_begin = pos; pos <= end; ++pos)
        {
            if (*pos == current_expect_end)
            {
                expects_end.pop_back();
                current_expect_end = expects_end.empty() ? 0 : expects_end.back();
            }
            else
            {
                switch (*pos)
                {
                    case '[':
                        current_expect_end = ']';
                        expects_end.push_back(current_expect_end);
                        break;
                    case '{':
                        current_expect_end = '}';
                        expects_end.push_back(current_expect_end);
                        break;
                    case '"' :
                        current_expect_end = '"';
                        expects_end.push_back(current_expect_end);
                        break;
                    case '\\':
                        /// skip backslash
                        if (pos + 1 < end && pos[1] == '"')
                            ++pos;
                        break;
                    default:
                        if (!current_expect_end && (*pos == ',' || *pos == '}'))
                        {
                            payload_begin = extract_begin;
                            payload_last = pos - 1;
                            while (payload_begin < payload_last && *payload_begin == ' ')
                                ++payload_begin;
                            while (payload_last > payload_begin && *payload_last == ' ')
                                --payload_last;
                            if (payload_begin < payload_last && *payload_begin == '"' && *payload_last == '"')
                            {
                                ++payload_begin;
                                --payload_last;
                            }

                            return pos;
                        }
                }
            }
        }
        return pos;
    }


    void extractJSONRecursive(
                       Pos begin,
                       Pos last,
                       std::string jsonPath,
                       size_t dot_position,
                       size_t previous_dot_position,
                       ColumnArray::Offsets & res_offsets,
                       ColumnString::Chars & res_strings_chars,
                       ColumnString::Offsets & res_strings_offsets,
                       ColumnArray::Offset & current_row_offset)
    {
        std::string jsonKey;

        if (std::string::npos == dot_position)
            jsonKey = "\"" + jsonPath.substr(previous_dot_position, std::string::npos) + "\":";
        else
            jsonKey = "\"" + jsonPath.substr(previous_dot_position, dot_position - previous_dot_position) + "\":";

        Pos pos = begin;

        Volnitsky searcher(jsonKey.data(), jsonKey.size(), begin - last + 1);

        /// We will search for the next occurrence in all strings at once.
        while (pos <= last && last > (pos = searcher.search(pos, last - pos + 1)))
        {
            Pos payload_begin = nullptr;
            Pos payload_last = nullptr;
            pos = findBeginLast(pos + jsonKey.size(), last + 1, payload_begin, payload_last);

            if (payload_begin && payload_last)
            {
                if (std::string::npos == dot_position) // no other key parts to search
                {
                    res_strings_chars.insert(payload_begin, payload_last + 1);
                    res_strings_chars.push_back(0);

                    res_strings_offsets.push_back(res_strings_chars.size());
                    ++current_row_offset;
                }
                else
                {
                    extractJSONRecursive(
                        payload_begin,
                        payload_last,
                        jsonPath,
                        jsonPath.find(".", dot_position + 1),
                        dot_position + 1,
                        res_offsets,
                        res_strings_chars,
                        res_strings_offsets,
                        current_row_offset);
                }
            }
        }
    }


    void extractMultiJSON(const ColumnString::Chars & data, const ColumnString::Offsets & offsets,
                       std::string needle,
                       ColumnArray::Offsets & res_offsets,
                       ColumnString::Chars & res_strings_chars,
                       ColumnString::Offsets & res_strings_offsets)
    {
        std::size_t dot_position = needle.find(".");
        const Pos data_begin = data.data();

        ColumnArray::Offset current_row_offset = 0;
        for(size_t row_index = 0; row_index < offsets.size(); ++row_index)
        {
            extractJSONRecursive(
                           data_begin,
                           data_begin + offsets[row_index] - 2,
                           needle,
                           dot_position,
                           0,
                           res_offsets,
                           res_strings_chars,
                           res_strings_offsets,
                           current_row_offset);
            res_offsets.push_back(current_row_offset);
        }
    }


    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result, size_t /*input_rows_count*/) override
    {
        size_t json_arg = arguments[0];
        size_t path_arg = arguments[1];

        if (!block.getByPosition(path_arg).column->isColumnConst())
            throw Exception(getName() + " currently supports only constant expressions as second argument", ErrorCodes::ILLEGAL_COLUMN);

        const ColumnConst * needle_str =
                checkAndGetColumnConstStringOrFixedString(block.getByPosition(path_arg).column.get());
        String needle = needle_str->getValue<String>();

        const auto maybe_const = block.getByPosition(json_arg).column.get()->convertToFullColumnIfConst();
        const ColumnString * json_str = checkAndGetColumn<ColumnString>(maybe_const.get());

        if (json_str)
        {
            const ColumnString::Chars & json_chars = json_str->getChars();
            const ColumnString::Offsets & json_offsets = json_str->getOffsets();

            auto col_res = ColumnArray::create(ColumnString::create());
            ColumnString & res_strings = typeid_cast<ColumnString &>(col_res->getData());
            ColumnArray::Offsets & res_offsets = col_res->getOffsets();
            ColumnString::Chars & res_strings_chars = res_strings.getChars();
            ColumnString::Offsets & res_strings_offsets = res_strings.getOffsets();

            extractMultiJSON(json_chars, json_offsets, needle, res_offsets, res_strings_chars, res_strings_offsets);

            block.getByPosition(result).column = std::move(col_res);
        }
        else
            throw Exception("Illegal columns " + block.getByPosition(0).column->getName()
                    + ", " + block.getByPosition(1).column->getName()
                    + " of arguments of function " + getName(),
                ErrorCodes::ILLEGAL_COLUMN);
    }
};



}

