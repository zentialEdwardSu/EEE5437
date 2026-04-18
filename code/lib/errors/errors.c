#include "errors/errors.h"

#include <string.h>

const char *dic_status_symbol(int status_code)
{
#define DIC_STATUS_SYMBOL_CASE(symbol, value, message) case value: return #symbol;
    switch (status_code)
    {
        DIC_STATUS_TABLE(DIC_STATUS_SYMBOL_CASE)
    default:
        return "DIC_UNKNOWN_STATUS";
    }

#undef DIC_STATUS_SYMBOL_CASE
}

const char *dic_status_message(int status_code)
{
#define DIC_STATUS_MESSAGE_CASE(symbol, value, message) case value: return message;
    switch (status_code)
    {
        DIC_STATUS_TABLE(DIC_STATUS_MESSAGE_CASE)
    default:
        return "unknown error";
    }

#undef DIC_STATUS_MESSAGE_CASE
}

const char *dic_status_message_from_symbol(const char *status_symbol)
{
    if (status_symbol == NULL)
        return "unknown error";

#define DIC_STATUS_SYMBOL_MESSAGE_MATCH(symbol, value, message)                  \
    if (strcmp(status_symbol, #symbol) == 0)                                     \
        return message;

    DIC_STATUS_TABLE(DIC_STATUS_SYMBOL_MESSAGE_MATCH)

#undef DIC_STATUS_SYMBOL_MESSAGE_MATCH

    return "unknown error";
}
