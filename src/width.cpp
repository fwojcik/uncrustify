/**
 * @file width.cpp
 * Limits line width.
 *
 * @author  Ben Gardner
 * @license GPL v2+
 */

#include "width.h"

#include "indent.h"
#include "newlines.h"
#include "prototypes.h"


constexpr static auto LCURRENT = LSPLIT;

using namespace uncrustify;


/**
 * abbreviations used:
 * - fparen = function parenthesis
 */

struct cw_entry
{
   Chunk  *pc;
   size_t pri;
};


struct token_pri
{
   E_Token tok;
   size_t  pri;
};


static inline bool is_past_width(Chunk *pc);


//! Split right after the chunk
static void split_before_chunk(Chunk *pc);


static size_t get_split_pri(E_Token tok);


/**
 * Checks to see if pc is a better spot to split.
 * This should only be called going BACKWARDS (ie prev)
 * A lower level wins
 *
 * Splitting Preference:
 *  - semicolon
 *  - comma
 *  - boolean op
 *  - comparison
 *  - arithmetic op
 *  - assignment
 *  - concatenated strings
 *  - ? :
 *  - function open paren not followed by close paren
 */
static void try_split_here(cw_entry &ent, Chunk *pc);


/**
 * Scan backwards to find the most appropriate spot to split the line
 * and insert a newline.
 *
 * See if this needs special function handling.
 * Scan backwards and find the best token for the split.
 *
 * @param start The first chunk that exceeded the limit
 * @return      The last chunk that was processed
 */
static Chunk *split_line(Chunk *pc);


/**
 * Figures out where to split a function def/proto/call.
 * This must not be called unless the function is known to
 * need splitting!
 *
 * For function prototypes and definition. Also function calls where
 * level == brace_level:
 *   - find the function's open parenthesis
 *   - find the function's matching close parenthesis
 *   - go through all chunks from open to close
 *     + remember valid split points along the way
 *       * valid split points are commas and function open parens,
 *         except that empty function parens '()' are not split
 *     + if a parameter doesn't fit on the current line, then
 *       split at the previous valid split point
 *   - If no splits happened, then force a split at the last splitpoint
 *
 * @param start   the chunk that exceeded the length limit
 * @return        the last chunk that was processed
 */
static Chunk *split_fcn_params(Chunk *start);


/**
 * Figures out where to split a template
 *
 *
 * @param start   the offending token
 */
static void split_template(Chunk *start);


/**
 * Splits the parameters at every comma that is at the fparen level.
 *
 * @param start   the offending token
 */
static void split_fcn_params_full(Chunk *start);


/**
 * A for statement is too long.
 * Step backwards and forwards to find the semicolons
 * Try splitting at the semicolons first.
 * If that doesn't work, then look for a comma at paren level.
 * If that doesn't work, then look for an assignment at paren level.
 * If that doesn't work, then give up.
 */
static void split_for_stmt(Chunk *start);


static inline bool is_past_width(Chunk *pc)
{
   // allow char to sit at last column by subtracting 1
   LOG_FMT(LSPLIT, "%s(%d): orig_line is %zu, orig_col is %zu, col is %zu, for %s\n",
           __func__, __LINE__, pc->orig_line, pc->orig_col, pc->column, pc->Text());
   log_rule_B("code_width");
   return((pc->column + pc->Len() - 1) > options::code_width());
}


static void split_before_chunk(Chunk *pc)
{
   LOG_FUNC_ENTRY();
   LOG_FMT(LSPLIT, "%s(%d): Text() '%s'\n", __func__, __LINE__, pc->Text());

   if (  !pc->IsNewline()
      && !pc->GetPrev()->IsNewline())
   {
      newline_add_before(pc);
      // reindent needs to include the indent_continue value and was off by one
      log_rule_B("indent_columns");
      log_rule_B("indent_continue");
      reindent_line(pc, pc->brace_level * options::indent_columns() +
                    abs(options::indent_continue()) + 1);
      cpd.changes++;
   }
}


void do_code_width()
{
   LOG_FUNC_ENTRY();
   LOG_FMT(LSPLIT, "%s(%d)\n", __func__, __LINE__);

   for (Chunk *pc = Chunk::GetHead(); pc->IsNotNullChunk(); pc = pc->GetNext())
   {
      if (  !pc->IsCommentOrNewline()
         && pc->IsNot(CT_SPACE)
         && is_past_width(pc))
      {
         if (  pc->Is(CT_VBRACE_CLOSE)  // don't break if a vbrace close
            && pc->IsLastChunkOnLine()) // is the last chunk on its line
         {
            continue;
         }
         Chunk *newpc = split_line(pc);

         if (newpc != nullptr)
         {
            pc = newpc;
            LOG_FMT(LSPLIT, "%s(%d): orig_line is %zu, orig_col is %zu, Text() '%s'\n",
                    __func__, __LINE__, pc->orig_line, pc->orig_col, pc->Text());
         }
         else
         {
            LOG_FMT(LSPLIT, "%s(%d): Bailed! orig_line is %zu, orig_col is %zu, Text() '%s'\n",
                    __func__, __LINE__, pc->orig_line, pc->orig_col, pc->Text());
            break;
         }
      }
   }
}


static const token_pri pri_table[] =
{
   { CT_SEMICOLON,    1 },
   { CT_COMMA,        2 },
   { CT_BOOL,         3 },
   { CT_COMPARE,      4 },
   { CT_SHIFT,        5 },
   { CT_ARITH,        6 },
   { CT_CARET,        7 },
   { CT_ASSIGN,       8 },
   { CT_STRING,       9 },
   { CT_FOR_COLON,   10 },
   //{ CT_DC_MEMBER, 11 },
   //{ CT_MEMBER,    11 },
   { CT_QUESTION,    20 }, // allow break in ? : for ls_code_width
   { CT_COND_COLON,  20 },
   { CT_FPAREN_OPEN, 21 }, // break after function open paren not followed by close paren
   { CT_QUALIFIER,   25 },
   { CT_CLASS,       25 },
   { CT_STRUCT,      25 },
   { CT_TYPE,        25 },
   { CT_TYPENAME,    25 },
   { CT_VOLATILE,    25 },
};


static size_t get_split_pri(E_Token tok)
{
   for (auto token : pri_table)
   {
      if (token.tok == tok)
      {
         return(token.pri);
      }
   }

   return(0);
}


static void try_split_here(cw_entry &ent, Chunk *pc)
{
   LOG_FUNC_ENTRY();

   size_t pc_pri = get_split_pri(pc->GetType());

   LOG_FMT(LSPLIT, "%s(%d): pc_pri is %zu\n", __func__, __LINE__, pc_pri);

   if (pc_pri == 0)
   {
      LOG_FMT(LSPLIT, "%s(%d): pc_pri is 0, return\n", __func__, __LINE__);
      return;
   }
   LOG_FMT(LSPLIT, "%s(%d):\n", __func__, __LINE__);
   // Can't split after a newline
   Chunk *prev = pc->GetPrev();

   if (  prev->IsNullChunk()
      || (  prev->IsNewline()
         && pc->IsNot(CT_STRING)))
   {
      if (prev->IsNotNullChunk())
      {
         LOG_FMT(LSPLIT, "%s(%d): Can't split after a newline, orig_line is %zu, return\n",
                 __func__, __LINE__, prev->orig_line);
      }
      return;
   }
   LOG_FMT(LSPLIT, "%s(%d):\n", __func__, __LINE__);

   // Can't split a function without arguments
   if (pc->Is(CT_FPAREN_OPEN))
   {
      Chunk *next = pc->GetNext();

      if (next->Is(CT_FPAREN_CLOSE))
      {
         LOG_FMT(LSPLIT, "%s(%d): Can't split a function without arguments, return\n", __func__, __LINE__);
         return;
      }
   }
   LOG_FMT(LSPLIT, "%s(%d):\n", __func__, __LINE__);

   // Only split concatenated strings
   if (pc->Is(CT_STRING))
   {
      Chunk *next = pc->GetNext();

      if (next->IsNot(CT_STRING))
      {
         LOG_FMT(LSPLIT, "%s(%d): Only split concatenated strings, return\n", __func__, __LINE__);
         return;
      }
   }
   LOG_FMT(LSPLIT, "%s(%d):\n", __func__, __LINE__);

   // keep common groupings unless ls_code_width
   log_rule_B("ls_code_width");

   if (  !options::ls_code_width()
      && pc_pri >= 20)
   {
      LOG_FMT(LSPLIT, "%s(%d): keep common groupings unless ls_code_width, return\n", __func__, __LINE__);
      return;
   }
   LOG_FMT(LSPLIT, "%s(%d):\n", __func__, __LINE__);

   // don't break after last term of a qualified type
   if (pc_pri == 25)
   {
      Chunk *next = pc->GetNext();

      if (  next->IsNot(CT_WORD)
         && (get_split_pri(next->GetType()) != 25))
      {
         LOG_FMT(LSPLIT, "%s(%d): don't break after last term of a qualified type, return\n", __func__, __LINE__);
         return;
      }
   }
   LOG_FMT(LSPLIT, "%s(%d):\n", __func__, __LINE__);
   // Check levels first
   bool change = false;

   if (  ent.pc == nullptr
      || pc->level < ent.pc->level)
   {
      LOG_FMT(LSPLIT, "%s(%d):\n", __func__, __LINE__);
      change = true;
   }
   else
   {
      if (pc_pri < ent.pri)
      {
         LOG_FMT(LSPLIT, "%s(%d):\n", __func__, __LINE__);
         change = true;
      }
   }
   LOG_FMT(LSPLIT, "%s(%d): change is %s\n", __func__, __LINE__, change ? "TRUE" : "FALSE");

   if (change)
   {
      LOG_FMT(LSPLIT, "%s(%d): do the change\n", __func__, __LINE__);
      ent.pc  = pc;
      ent.pri = pc_pri;
   }
} // try_split_here


static Chunk *split_line(Chunk *start)
{
   LOG_FUNC_ENTRY();
   LOG_FMT(LSPLIT, "%s(%d): start->Text() '%s', orig_line is %zu, orig_col is %zu, col is %zu, type is %s\n",
           __func__, __LINE__, start->Text(), start->orig_line, start->orig_col, start->column, get_token_name(start->GetType()));
   LOG_FMT(LSPLIT, "   start->GetFlags() ");
   log_pcf_flags(LSPLIT, start->GetFlags());
   LOG_FMT(LSPLIT, "   start->GetParentType() %s, (PCF_IN_FCN_DEF is %s), (PCF_IN_FCN_CALL is %s)\n",
           get_token_name(start->GetParentType()),
           start->TestFlags((PCF_IN_FCN_DEF)) ? "TRUE" : "FALSE",
           start->TestFlags((PCF_IN_FCN_CALL)) ? "TRUE" : "FALSE");

   // break at maximum line length if ls_code_width is true
   // Issue #2432
   if (start->TestFlags(PCF_ONE_LINER))
   {
      Chunk *next;
      LOG_FMT(LSPLIT, "%s(%d): ** ONCE LINER SPLIT **\n", __func__, __LINE__);
      next = undo_one_liner(start);
      newlines_cleanup_braces(false);
      // Issue #1352
      cpd.changes++;
      // This line isn't split yet, but it will be next main loop.
      // We don't give up splitting lines here yet, but keep going from the end of
      // this one-liner. This prevents many long one-liners from turning uncrustify
      // into an O(N^2) operation. It also helps to prevent problems if there are
      // aligned chunks after the eventual split point.
      return(next);
   }
   LOG_FMT(LSPLIT, "%s(%d): before ls_code_width\n", __func__, __LINE__);

   log_rule_B("ls_code_width");

   if (options::ls_code_width())
   {
   }
   // Check to see if we are in a for statement
   else if (start->TestFlags(PCF_IN_FOR))
   {
      LOG_FMT(LSPLIT, " ** FOR SPLIT **\n");
      split_for_stmt(start);

      if (!is_past_width(start))
      {
         return(start);
      }
      LOG_FMT(LSPLIT, "%s(%d): for split didn't work\n", __func__, __LINE__);
   }

   /*
    * If this is in a function call or prototype, split on commas or right after the
    * open parenthesis. Note that this block of if() tests means that if the "FUNC
    * SPLIT" block is entered, then start cannot be before the opening paren nor
    * after the closing paren, unless it is a semicolon that immediately follows the
    * closing paren.
    *
    * The reason for the the level checking on function calls is because calls which
    * are not at the top of the current brace level (e.g. function calls inside an
    * if() statement) do not get split in this block; they fall through to the
    * generic line-splitting logic below.
    *
    * Similarly, since this "FUNC SPLIT" block should only be entered if we are
    * certain a newline should be added, an opening paren for a function call inside
    * a function call should also fall through. This is mostly to handle the case
    * where a function ends with something like "...., someOtherFn())));". If another
    * splittable location exists which is_past_width(), then split_line() will be
    * called again on it eventually.
    *
    * Technically, if start is a semicolon then the ParentType cannot be CT_FUNC_DEF,
    * but all other cases in that part of the if() are possible, so leaving the logic
    * as-is is just cleaner.
    */
   else if (  start->TestFlags(PCF_IN_FCN_DEF)
           || (  start->TestFlags(PCF_IN_FCN_CALL)
              && (start->level == (start->brace_level + 1)))
           || (  !start->TestFlags(PCF_IN_FCN_CALL)
              && (  (  start->Is(CT_FPAREN_OPEN)
                    || start->IsSemicolon())
                 && (  (start->GetParentType() == CT_FUNC_PROTO) // Issue #1169
                    || (start->GetParentType() == CT_FUNC_DEF)
                    || (start->GetParentType() == CT_FUNC_CALL)))))
   {
      LOG_FMT(LSPLIT, " ** FUNC SPLIT **\n");

      log_rule_B("ls_func_split_full");

      if (options::ls_func_split_full())
      {
         split_fcn_params_full(start);

         if (!is_past_width(start))
         {
            return(start);
         }
      }
      return(split_fcn_params(start));
   }

   /*
    * If this is in a template, split on commas, Issue #1170
    */
   else if (start->TestFlags(PCF_IN_TEMPLATE))
   {
      LOG_FMT(LSPLIT, " ** TEMPLATE SPLIT **\n");
      split_template(start);
      return(start);
   }
   LOG_FMT(LSPLIT, "%s(%d):\n", __func__, __LINE__);
   // Try to find the best spot to split the line
   cw_entry ent;

   memset(&ent, 0, sizeof(ent));
   Chunk *pc = start;
   Chunk *prev;

   while (  ((pc = pc->GetPrev()) != nullptr)
         && pc->IsNotNullChunk()
         && !pc->IsNewline())
   {
      LOG_FMT(LSPLIT, "%s(%d): at %s, orig_line is %zu, orig_col is %zu, col is %zu\n",
              __func__, __LINE__, pc->Text(), pc->orig_line, pc->orig_col, pc->column);

      if (pc->IsNot(CT_SPACE))
      {
         try_split_here(ent, pc);

         // break at maximum line length
         log_rule_B("ls_code_width");

         if (  ent.pc != nullptr
            && (options::ls_code_width()))
         {
            break;
         }
      }
   }

   if (ent.pc == nullptr)
   {
      LOG_FMT(LSPLIT, "%s(%d):    TRY_SPLIT yielded NO SOLUTION for orig_line %zu at '%s' [%s]\n",
              __func__, __LINE__, start->orig_line, start->Text(), get_token_name(start->GetType()));
   }
   else
   {
      LOG_FMT(LSPLIT, "%s(%d):    TRY_SPLIT yielded '%s' [%s] on orig_line %zu\n",
              __func__, __LINE__, ent.pc->Text(), get_token_name(ent.pc->GetType()), ent.pc->orig_line);
      LOG_FMT(LSPLIT, "%s(%d): ent at '%s', orig_col is %zu, col is %zu\n",
              __func__, __LINE__, ent.pc->Text(), ent.pc->orig_col, ent.pc->column);
   }

   // Break before the token instead of after it according to the pos_xxx rules
   if (ent.pc == nullptr)
   {
      pc = nullptr;
   }
   else
   {
      log_rule_B("pos_arith");
      log_rule_B("pos_assign");
      log_rule_B("pos_compare");
      log_rule_B("pos_conditional");
      log_rule_B("pos_shift");
      log_rule_B("pos_bool");

      if (  (  ent.pc->Is(CT_SHIFT)
            && (options::pos_shift() & TP_LEAD))
         || (  (  ent.pc->Is(CT_ARITH)
               || ent.pc->Is(CT_CARET))
            && (options::pos_arith() & TP_LEAD))
         || (  ent.pc->Is(CT_ASSIGN)
            && (options::pos_assign() & TP_LEAD))
         || (  ent.pc->Is(CT_COMPARE)
            && (options::pos_compare() & TP_LEAD))
         || (  (  ent.pc->Is(CT_COND_COLON)
               || ent.pc->Is(CT_QUESTION))
            && (options::pos_conditional() & TP_LEAD))
         || (  ent.pc->Is(CT_BOOL)
            && (options::pos_bool() & TP_LEAD)))
      {
         pc = ent.pc;
      }
      else
      {
         pc = ent.pc->GetNext();
      }
      LOG_FMT(LSPLIT, "%s(%d): at '%s', orig_col is %zu, col is %zu\n",
              __func__, __LINE__, pc->Text(), pc->orig_col, pc->column);
   }

   if (  pc == nullptr
      || pc->IsNullChunk())
   {
      pc = start;

      // Don't break before a close, comma, or colon
      if (  start->Is(CT_PAREN_CLOSE)
         || start->Is(CT_PAREN_OPEN)
         || start->Is(CT_FPAREN_CLOSE)
         || start->Is(CT_FPAREN_OPEN)
         || start->Is(CT_SPAREN_CLOSE)
         || start->Is(CT_SPAREN_OPEN)
         || start->Is(CT_ANGLE_CLOSE)
         || start->Is(CT_BRACE_CLOSE)
         || start->Is(CT_COMMA)
         || start->IsSemicolon()
         || start->Len() == 0)
      {
         LOG_FMT(LSPLIT, " ** NO GO **\n");

         // TODO: Add in logic to handle 'hard' limits by backing up a token
         return(start);
      }
   }
   // add a newline before pc
   prev = pc->GetPrev();

   if (  prev->IsNotNullChunk()
      && !pc->IsNewline()
      && !prev->IsNewline())
   {
      //int plen = (pc->Len() < 5) ? pc->Len() : 5;
      //int slen = (start->Len() < 5) ? start->Len() : 5;
      //LOG_FMT(LSPLIT, " '%.*s' [%s], started on token '%.*s' [%s]\n",
      //        plen, pc->Text(), get_token_name(pc->GetType()),
      //        slen, start->Text(), get_token_name(start->GetType()));
      LOG_FMT(LSPLIT, "%s(%d): Text() '%s', type %s, started on token '%s', type %s\n",
              __func__, __LINE__, pc->Text(), get_token_name(pc->GetType()),
              start->Text(), get_token_name(start->GetType()));

      split_before_chunk(pc);
   }
   return(start);
} // split_line


/*
 * The for statement split algorithm works as follows:
 *   1. Step backwards and forwards to find the semicolons
 *   2. Try splitting at the semicolons first.
 *   3. If that doesn't work, then look for a comma at paren level.
 *   4. If that doesn't work, then look for an assignment at paren level.
 *   5. If that doesn't work, then give up.
 */
static void split_for_stmt(Chunk *start)
{
   LOG_FUNC_ENTRY();
   // how many semicolons (1 or 2) do we need to find
   log_rule_B("ls_for_split_full");
   size_t max_cnt     = options::ls_for_split_full() ? 2 : 1;
   Chunk  *open_paren = nullptr;
   size_t nl_cnt      = 0;

   LOG_FMT(LSPLIT, "%s: starting on %s, line %zu\n",
           __func__, start->Text(), start->orig_line);

   // Find the open paren so we know the level and count newlines
   Chunk *pc = start;

   while ((pc = pc->GetPrev())->IsNotNullChunk())
   {
      if (pc->Is(CT_SPAREN_OPEN))
      {
         open_paren = pc;
         break;
      }

      if (pc->nl_count > 0)
      {
         nl_cnt += pc->nl_count;
      }
   }

   if (open_paren == nullptr)
   {
      LOG_FMT(LSPLIT, "No open paren\n");
      return;
   }
   // see if we started on the semicolon
   int   count = 0;
   Chunk *st[2];

   pc = start;

   if (  pc->Is(CT_SEMICOLON)
      && pc->GetParentType() == CT_FOR)
   {
      st[count++] = pc;
   }

   // first scan backwards for the semicolons
   while (  (count < static_cast<int>(max_cnt))
         && ((pc = pc->GetPrev())->IsNotNullChunk())
         && pc->IsNotNullChunk()
         && pc->TestFlags(PCF_IN_SPAREN))
   {
      if (  pc->Is(CT_SEMICOLON)
         && pc->GetParentType() == CT_FOR)
      {
         st[count++] = pc;
      }
   }
   // And now scan forward
   pc = start;

   while (  (count < static_cast<int>(max_cnt))
         && ((pc = pc->GetNext())->IsNotNullChunk())
         && pc->TestFlags(PCF_IN_SPAREN))
   {
      if (  pc->Is(CT_SEMICOLON)
         && pc->GetParentType() == CT_FOR)
      {
         st[count++] = pc;
      }
   }

   while (--count >= 0)
   {
      // TODO: st[0] may be uninitialized here
      LOG_FMT(LSPLIT, "%s(%d): split before %s\n", __func__, __LINE__, st[count]->Text());
      split_before_chunk(st[count]->GetNext());
   }

   if (  !is_past_width(start)
      || nl_cnt > 0)
   {
      return;
   }
   // Still past width, check for commas at parentheses level
   pc = open_paren;

   while ((pc = pc->GetNext()) != start)
   {
      if (  pc->Is(CT_COMMA)
         && (pc->level == (open_paren->level + 1)))
      {
         split_before_chunk(pc->GetNext());

         if (!is_past_width(pc))
         {
            return;
         }
      }
   }
   // Still past width, check for a assignments at parentheses level
   pc = open_paren;

   while ((pc = pc->GetNext()) != start)
   {
      if (  pc->Is(CT_ASSIGN)
         && (pc->level == (open_paren->level + 1)))
      {
         split_before_chunk(pc->GetNext());

         if (!is_past_width(pc))
         {
            return;
         }
      }
   }
   // Oh, well. We tried.
} // split_for_stmt


static void split_fcn_params_full(Chunk *start)
{
   LOG_FUNC_ENTRY();
   LOG_FMT(LSPLIT, "%s(%d): %s\n", __func__, __LINE__, start->Text());

   // Find the opening function parenthesis
   Chunk *fpo = start;

   LOG_FMT(LSPLIT, "  %s(%d): Find the opening function parenthesis\n", __func__, __LINE__);

   while ((fpo = fpo->GetPrev())->IsNotNullChunk())
   {
      LOG_FMT(LSPLIT, "%s(%d): %s, orig_col is %zu, col is %zu, level is %zu\n",
              __func__, __LINE__, fpo->Text(), fpo->orig_col, fpo->column, fpo->level);

      if (  fpo->Is(CT_FPAREN_OPEN)
         && (fpo->level == start->level - 1))
      {
         break;  // opening parenthesis found. Issue #1020
      }
   }
   // Now break after every comma
   Chunk *pc = fpo->GetNextNcNnl();

   while (pc->IsNotNullChunk())
   {
      if (pc->level <= fpo->level)
      {
         break;
      }

      if (  (pc->level == (fpo->level + 1))
         && pc->Is(CT_COMMA))
      {
         split_before_chunk(pc->GetNext());
      }
      pc = pc->GetNextNcNnl();
   }
}


static void split_fcn_params_greedy(Chunk *fpo, Chunk *fpc)
{
   LOG_FUNC_ENTRY();

   log_rule_B("code_width");

   Chunk  *end           = fpc->GetNext();  // Make sure that fpc is processed by the loop below.
   Chunk  *splitpoint    = fpo;             // The opening fparen is a valid place to split the fcn.
   size_t min_col        = fpo->GetNextNcNnl()->column;
   int    hard_limit     = static_cast<int>(options::code_width());
   int    added_newlines = 0;

   LOG_FMT(LSPLIT, "%s(%d): fpo->Text() is '%s', orig_line is %zu, orig_col is %zu, col is %zu\n",
           __func__, __LINE__, fpo->Text(), fpo->orig_line, fpo->orig_col, fpo->column);
   LOG_FMT(LSPLIT, "%s(%d): fpc->Text() is '%s', orig_line is %zu, orig_col is %zu, col is %zu\n",
           __func__, __LINE__, fpc->Text(), fpc->orig_line, fpc->orig_col, fpc->column);
   LOG_FMT(LSPLIT, "%s(%d): splitpoint->Text() is '%s', orig_line is %zu, orig_col is %zu, col is %zu\n",
           __func__, __LINE__, splitpoint->Text(), splitpoint->orig_line, splitpoint->orig_col, splitpoint->column);
   LOG_FMT(LSPLIT, "    min_col is %zu, max_width is %d\n",
           min_col, hard_limit);

   for (Chunk *pc = fpo; pc != end; pc = pc->GetNext())
   {
      LOG_FMT(LSPLIT, "%s(%d): pc is now '%s' from orig_line %zu, orig_col %zu, cur_col %zu\n",
              __func__, __LINE__, pc->Text(), pc->orig_line, pc->orig_col, pc->column);

      if (pc->IsNewline())
      {
         splitpoint = nullptr;
         LOG_FMT(LSPLIT, "%s(%d): Newline; resetting splitpoint\n", __func__, __LINE__);
         continue;
      }
      // Only try splitting when we encounter a comma or fparen, and only split at
      // commas or opening fparens that aren't part of empty parens '()'.
      //
      // Also keep track of current continuation indent amount when we encounter an fparen.
      // XXX I am not at all sure this logic is correct, since fparens don't change
      // the brace_level and that's the only way min_col can change aside from
      // options (which never change at runtime). But this is how it was done in the
      // previous code, so I'm keeping it.
      bool ok_to_split_here = true;

      if (  pc->Is(CT_FPAREN_OPEN)
         || (pc->Is(CT_FPAREN_CLOSE)))
      {
         if (pc->Is(CT_FPAREN_OPEN))
         {
            // Don't split '()'
            if (pc->GetNext()->Is(CT_FPAREN_CLOSE))
            {
               ok_to_split_here = false;
            }
         }
         else
         {
            // Don't split on ')', but do recompute min_col and do split lines
            // that are too-long and splittable.
            ok_to_split_here = false;
         }
         LOG_FMT(LSPLIT, "%s(%d): Recomputing min_col from %zu\n",
                 __func__, __LINE__, min_col);

         log_rule_B("indent_paren_nl");

         if (!options::indent_paren_nl())
         {
            log_rule_B("indent_columns");
            min_col = pc->GetNext()->brace_level * options::indent_columns() + 1;
            LOG_FMT(LSPLIT, "%s(%d): min_col is %zu\n",
                    __func__, __LINE__, min_col);

            log_rule_B("indent_continue");

            if (options::indent_continue() == 0)
            {
               log_rule_B("indent_columns");
               min_col += options::indent_columns();
            }
            else
            {
               min_col += abs(options::indent_continue());
            }
            LOG_FMT(LSPLIT, "%s(%d): min_col is %zu\n",
                    __func__, __LINE__, min_col);
         }
      }
      else if (!pc->Is(CT_COMMA))
      {
         continue;
      }

      // If we don't have a valid splitpoint, then it doesn't matter how long the
      // line currently is, because we can't split it anywhere. So just remember
      // this as the next valid splitpoint.
      //
      // If this chunk doesn't exceed the length limit, then remember the fact that
      // we can split this line here. However, if we've hit the closing fparen
      // without splitting the line so far, then force a split, since we were called
      // at all and so a split must be necessary.
      if (  (splitpoint == nullptr)
         || (  !is_past_width(pc)
            && (  (pc != fpc)
               || (added_newlines > 0))))
      {
         if (ok_to_split_here)
         {
            LOG_FMT(LSPLIT, "%s(%d): Setting splitpoint\n",
                    __func__, __LINE__);
            splitpoint = pc;
         }
         continue;
      }
      // Since we need to split this line and we have a valid place to split it, do
      // that by adding a newline after the splitpoint and reindenting the remainder
      // of the line. Then keep splitting this fcn by looping again from the
      // splitpoint. Note that this ensures that the newline just added is the next
      // chunk processed by the loop, which will reset splitpoint.
      pc = splitpoint->GetNext();

      // Don't bother splitting the line if it is already split.
      if (!pc->IsNewline())
      {
         LOG_FMT(LSPLIT, "%s(%d): Splitting long line before '%s', orig_line %zu, orig_col %zu, col %zu\n",
                 __func__, __LINE__, pc->Text(), pc->orig_line, pc->orig_col, pc->column);
         newline_add_before(pc);
         reindent_line(pc, min_col);
         cpd.changes++;
         added_newlines++;
      }
      pc = splitpoint;
   }

   LOG_FMT(LSPLIT, "%s(%d): Completed splitting\n",
           __func__, __LINE__);
} // split_fcn_params_greedy


static Chunk *split_fcn_params(Chunk *start)
{
   LOG_FUNC_ENTRY();
   LOG_FMT(LSPLIT, "%s(%d): start->Text() is '%s', orig_line is %zu, orig_col is %zu, col is %zu\n",
           __func__, __LINE__, start->Text(), start->orig_line, start->orig_col, start->column);
   Chunk *fpo = start;

   if (!start->Is(CT_FPAREN_OPEN))
   {
      // Find the opening function parenthesis
      LOG_FMT(LSPLIT, "%s(%d): Find the opening function parenthesis\n", __func__, __LINE__);
      size_t level = (start->IsSemicolon() || start->Is(CT_FPAREN_CLOSE)) ? start->level
              : start->level - 1;
      fpo = fpo->GetPrevType(CT_FPAREN_OPEN, level);

      if (fpo->IsNullChunk())
      {
         fprintf(stderr, "%s(%d): Can't find fparen_open; bailing\n",
                 __func__, __LINE__);
         log_flush(true);
         exit(EX_SOFTWARE);
      }
   }
   // Find the closing fparen that matches fpo.
   Chunk *fpc = fpo->GetNextType(CT_FPAREN_CLOSE, fpo->level);

   if (fpc->IsNullChunk())
   {
      fprintf(stderr, "%s(%d): Can't find fparen_close; bailing\n",
              __func__, __LINE__);
      log_flush(true);
      exit(EX_SOFTWARE);
   }
   split_fcn_params_greedy(fpo, fpc);

   // If we found the expected FPAREN_OPEN, then we only processed through the
   // corresponding FPAREN_CLOSE. Otherwise, we processed one chunk beyond that since
   // that's how we were called.
   return((fpo->level == start->level) ? fpc->GetNext() : fpc);
} // split_fcn_params


static void split_template(Chunk *start)
{
   LOG_FUNC_ENTRY();
   LOG_FMT(LSPLIT, "  %s(%d): start %s\n", __func__, __LINE__, start->Text());
   LOG_FMT(LSPLIT, "  %s(%d): back up until the prev is a comma\n", __func__, __LINE__);

   // back up until the prev is a comma
   Chunk *prev = start;

   while ((prev = prev->GetPrev())->IsNotNullChunk())
   {
      LOG_FMT(LSPLIT, "  %s(%d): prev '%s'\n", __func__, __LINE__, prev->Text());

      if (  prev->IsNewline()
         || prev->Is(CT_COMMA))
      {
         break;
      }
   }

   if (  prev->IsNotNullChunk()
      && !prev->IsNewline())
   {
      LOG_FMT(LSPLIT, "  %s(%d):", __func__, __LINE__);
      LOG_FMT(LSPLIT, " -- ended on %s --\n", get_token_name(prev->GetType()));
      Chunk  *pc = prev->GetNext();
      newline_add_before(pc);
      size_t min_col = 1;

      log_rule_B("indent_continue");

      if (options::indent_continue() == 0)
      {
         log_rule_B("indent_columns");
         min_col += options::indent_columns();
      }
      else
      {
         min_col += abs(options::indent_continue());
      }
      reindent_line(pc, min_col);
      cpd.changes++;
   }
} // split_templatefcn_params
