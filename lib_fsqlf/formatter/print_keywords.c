#include <string.h>     // strlen
#include <ctype.h>      // tolower, toupper
#include <stdio.h>      // fprintf, fputs
#include <assert.h>      // assert
#include "print_keywords.h"
#include "globals.h"



// Return max argument.
// Strange name is chosen just to make it unique,
// so it would not clash with some compiler/platform specific macros.
static int max_2args_(int a, int b)
{
    return a > b ? a : b;
}


static int max_or_current(int prev_count, int curr_count, char use_only_curr_ind)
{
    if (use_only_curr_ind) {
        return curr_count;
    }
    else{
        return max_2args_(prev_count, curr_count);
    }
}


static struct spacing calculate_spacing(
    struct spacing afterspacing_of_prev,
    unsigned short int isword_of_prev,
    struct spacing beforespacing_of_current,
    unsigned short int isword_of_current,
    int global_indent_level)
{
    /* Combine spacings of adjacent words (previous and current).
        Combinations are done in two ways:
            - take MAX
            - use only current words settings
            (one more option could be to
            first print previous word's 'after' spacing,
            then print current word's 'before' spacing, but this could/would
            result in trailing spaces on a line)
        Decision, which of them to use, is made so:
            There are 3 types of spacings.
            They are ranked this way: space-1, indentation-2, new line-3.
            If current word has some non-zero spacing setting,
            then for spacings of lower ranks use conf only from current word.
        Rationale for such logic:
            If word has in its settings new-line=1 and indent/spaces=0,
            then there is expectation that keyword will be
            at the start of the line, no matter what previous kw setting
            for indentation after it was.
            (except for global indent level, because of subselect)
    */
    struct spacing r; // result to be built


    r.new_line = max_2args_(afterspacing_of_prev.new_line, beforespacing_of_current.new_line);

    r.indent = max_or_current(
            afterspacing_of_prev.indent
            , beforespacing_of_current.indent
            , beforespacing_of_current.new_line>1);
    r.indent += r.new_line ? global_indent_level : 0;

    r.space = max_or_current(
            afterspacing_of_prev.space
            , beforespacing_of_current.space
            , beforespacing_of_current.new_line || beforespacing_of_current.indent);


    // adjacent words MUST have some spacing
    if (!r.new_line && !r.indent && !r.space &&
        isword_of_prev && isword_of_current) {
        r.space = 1;
    }

    return r;
}


#define MAX_KEYWORD_SIZE (200)
const char *stocase(const char* s_text, unsigned short int s_case)
{
    assert(strlen(s_text) < MAX_KEYWORD_SIZE);
    static char formatted_result[MAX_KEYWORD_SIZE];
    int i;

    switch (s_case) {
        case CASE_lower:
            for (i = 0; i<strlen(s_text); i++) {
                formatted_result[i] = tolower(s_text[i]);
            }
            break;
        case CASE_UPPER:
            for (i = 0; i<strlen(s_text); i++) {
                formatted_result[i] = toupper(s_text[i]);
            }
            break;
        case CASE_Initcap:
            formatted_result[0] = toupper(s_text[0]);
            for (i = 1; i<strlen(s_text); i++) {
                formatted_result[i] = tolower(s_text[i]);
            }
            break;
        case CASE_none:
            return s_text;
    }
    formatted_result[strlen(s_text)] = '\0';
    return formatted_result;
}


static void print_ntimes(FILE *yyout, const char *txt, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        fputs(txt, yyout);
    }
}


static void print_struct_spacing_count(FILE * yyout, struct spacing cnt)
{
    const char *newline_char = "\n";
    const char *indent_chars = "    "; // 4 spaces.
    const char *simple_space = " ";
    print_ntimes(yyout, newline_char, cnt.new_line);
    print_ntimes(yyout, indent_chars, cnt.indent);
    print_ntimes(yyout, simple_space, cnt.space);
}


// All printing of spacing goes through here.
// Except when space is inside the multiword-keyword (e.g. "LEFT JOIN"),
// those will not be printed by this function.
// ('spacing' means new lines, tabs and spaces)
static void print_spacing(
    FILE *yyout,
    struct kw_conf current_settings,
    int global_indent_level)
{
    // keep track of 'after' spacing from previous call
    static struct spacing from_previous__scounts = {0, 0, 0};
    // keep track of previous 'is_word'
    static unsigned short int from_previous__isword = 0;

    struct spacing spacing =
        calculate_spacing(
            from_previous__scounts,
            from_previous__isword,
            current_settings.before,
            current_settings.is_word,
            global_indent_level
        );

    print_struct_spacing_count(yyout, spacing);

    // Save settings for next function call - overwrite
    from_previous__scounts = current_settings.after;
    from_previous__isword = current_settings.is_word;
}


static void kw_print(FILE *yyout, const char *yytext, struct kw_conf s)
{
    int i = 0;
    // Call keyword specific functions, before printing.
    for (i = 0; i < KW_FUNCT_ARRAY_SIZE && s.funct_before[i] != NULL ; i++) {
        s.funct_before[i]();
    }

    // Print spacing.
    print_spacing(yyout, s, currindent); // print spacing before keyword

    // Print text:
    // .. first decide what text to use (original or default)
    const char *text_nocase = s.print_original_text ? yytext : s.text;
    // .. then handle its case
    const char *text = stocase(text_nocase, s.print_case);
    // .. then print the text.
    fprintf(yyout, "%s", text);

    // Call keyword specific functions. after printing.
    for (i = 0; i < KW_FUNCT_ARRAY_SIZE && s.funct_after[i] != NULL; i++) {
        s.funct_after[i]();
    }
}


static void echo_print(FILE *yyout, char *txt)
{
    int length; // length of the input text string
    length = strlen(txt);

    // Adjustment for single line comments.
    // Necessary for keeping indentation and new lines right.
    int pos_last_char = length - 1; // position of last character

    // Printing of spacing is delegated to print_spacing(),
    // which requires as input struct kw_conf.
    struct kw_conf s = {{0, 0, 0}, {0, 0, 0}, 0, 0, 0, 0};

    // Delegate to print_spacing() printing of the new line.
    if (txt[pos_last_char] == '\n') {
        // Shorten the string - overwrite \n with \0 (end of string mark).
        txt[pos_last_char] = '\0';
        // Adjust setting used by print_spacing()
        s.after.new_line = 1;
    }

    // Word-vs-operator check.
    // Ensure that two adjacent words have spacing inbetween.
    s.is_word = !(length == 1 && !isalnum(txt[0]));

    // print spacing then text
    print_spacing(yyout, s, currindent);
    fputs(txt, yyout);
}


void handle_kw(FILE *yyout, const char *yytext, const struct kw_conf *s)
{
    kw_print(yyout, yytext, *s);
}


void handle_text(FILE *yyout, char *txt)
{
    echo_print(yyout, txt);
}
