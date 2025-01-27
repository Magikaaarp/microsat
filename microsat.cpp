/*********************************************************************[microsat.c]***

  The MIT License

  Copyright (c) 2014-2018 Marijn Heule

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*************************************************************************************/

#include <stdio.h>
#include <stdlib.h>

enum { END = -9, UNSAT = 0, SAT = 1, MARK = 2, IMPLIED = 6, NOT_IMPLIED = 5};

struct solver
{
    int *DB, nVars, nClauses, mem_used, mem_fixed, mem_max;
    int maxLemmas, nLemmas, *buffer, nConflicts, *model;
    int *reason, *falseStack, *fals, *first, *forced, *processed, *assigned;
    int *next, *prev, head, fast, slow;
};

int* getMemory (struct solver* S, int mem_size)
{                                                                     // Allocate memory of size mem_size
    if (S->mem_used + mem_size > S->mem_max)
    {                                                                 // In case the code is used within a code base
        printf("c out of memory\n");
        exit(0);
    }
    int *store = (S->DB + S->mem_used);                               // Compute a pointer to the new memory location
    S->mem_used += mem_size;                                          // Update the size of the used memory
    return store;
}

void unassign(struct solver* S, int lit)
{
    S->fals[lit] = 0;
}

void assign(struct solver* S, int* reason, bool forced)
{                                                                     // Make the first literal of the reason true
    int lit = reason[0];                                              // Let lit be the first literal in the reason
    S->fals[-lit] = forced ? IMPLIED : 1;                             // Mark lit as true and IMPLIED if forced
    *(S->assigned++) = -lit;                                          // Push it on the assignment stack
    S->reason[abs(lit)] = (int)((reason)-S->DB);                      // Set the reason clause of lit
    S->model [abs(lit)] = (lit > 0);                                  // Mark the literal as true in the model
}

void restart(struct solver* S)
{                                                                     // unassign all variables
    while (S->assigned > S->forced)
        unassign(S, *(--S->assigned));                                // Remove all unforced fals lits from falseStack
    S->processed = S->forced;                                         // Reset the processed pointer
}

void addWatch(struct solver* S, int lit, int mem)
{               
    S->DB[mem] = S->first[lit];                                       // Add a watch pointer to a clause containing lit
    S->first[lit] = mem;                                              // By updating the database and the pointers
}               

int* addClause(struct solver* S, int* in, int size, int irr) 
{                                                                     // Adds a clause stored in *in of size size
    int i, used = S->mem_used;                                        // Store a pointer to the beginning of the clause
    int* clause = getMemory (S, size + 3) + 2;                        // Allocate memory for the clause in the database
    if (size >  1)                                                    // Two watch pointers to the datastructure
    {
        addWatch (S, in[0], used  );                                  // If the clause is not unit, then add the first two
        addWatch (S, in[1], used+1);
    }
    for (i = 0; i < size; i++)
        clause[i] = in[i];
    clause[i] = 0;                                                    // Copy the clause from the buffer to the database
    if (irr)                                                          //// If the clause is from the input, update [mem_fixed]
        S->mem_fixed = S->mem_used;
    else S->nLemmas++;                                                //// The number of learned clauses increased
    return clause;
}                                                                     // Return the pointer to the clause in the database

void reduceDB(struct solver *S, int k)
{                                                                     // Removes "less useful" lemmas from DB 
    while (S->nLemmas > S->maxLemmas)
        S->maxLemmas += 300;                                          // Allow more lemmas in the future
    S->nLemmas = 0;                                                   // Reset the number of learned lemmas
    for (int i = -S->nVars; i <= S->nVars; i++)
    {                                                                 // Loop over the variables
        if (i == 0) continue;
        int *watch = &S->first[i];                                    // Get the pointer to the first watched clause
        while (*watch >= S->mem_fixed)                                // According to the characteristic of stack
			*watch =  S->DB[*watch];                                  // Remove the watch if it points to a lemma
    }
    int old_used = S->mem_used;
    S->mem_used = S->mem_fixed;                                       // Virtually remove all lemmas
    for (int i = S->mem_fixed + 2; i < old_used; i += 3)
    {                                                                 // While the old memory contains lemmas
        int count = 0, head = i, lit;                                 // Get the lemma to which the head is pointing
        while ((lit = S->DB[i++]))                                    // Count the number of literals that are satisfied by the current model
            if ((lit > 0) == S->model[abs(lit)])
                count++;
        if (count < k)                                                // If the latter is smaller than k, add it back
            addClause(S, S->DB+head, i-head, 0);
    }
}

void bump(struct solver* S, int lit)                                  // Move the variable to the front of the decision list and MARK it
{
    if (S->fals[lit] != IMPLIED)
    {
        S->fals[lit] = MARK;                                          // MARK the literal as involved if not a top-level unit
        int var = abs(lit);
        if (var != S->head)                                           // In case var is not already the head of the list
        {
            S->prev[S->next[var]] = S->prev[var];                     //// Delete [var] from the link
            S->next[S->prev[var]] = S->next[var];                    
            S->next[S->head] = var;                                   //// Add [var] next to the head, and make it new head
            S->prev[var] = S->head;
            S->head = var;
        }
    }
}

bool implied(struct solver* S, int lit)                               //// Check if lit(eral) is implied (to be false) by MARK literals
{ 
    if (S->fals[lit] > MARK)                                          // If checked before, return old result
		return (S->fals[lit] == IMPLIED);
    if (!S->reason[abs(lit)]) return false;                           // In case lit is a decision or unassigned, it is 'not implied'
    int *p = (S->DB + S->reason[abs(lit)]);                           // Get the reason of lit(eral)
    while (*(++p))                                                    //// Traverse the other literals in the reason
        if ((S->fals[*p] ^ MARK) && !implied(S, *p))
		{                                                             // Recursively check if non-MARK literals are implied
        	S->fals[lit] = NOT_IMPLIED;
			return false;
		}
    S->fals[lit] = IMPLIED;
	return true;
}

int* analyze(struct solver* S, int* clause)                           // Compute a resolvent from falsified clause
{
	S->nConflicts++;                                                  // Bump restarts and update the statistic
    while (*clause)
		bump(S, *(clause++));                                         //// MARK all literals in the falsified clause
    while (S->reason[abs(*(--S->assigned))])
	{                                                                 // Loop on variables on falseStack until the last decision
    	if (S->fals[*S->assigned] == MARK)
		{                                                             // If the tail of the stack is MARK
        	int *check = S->assigned;                                 // Pointer to check if first-UIP is reached
            // while (S->fals[*(--check)]!=MARK)                      //// Check for only one MARK literal before decision (included)
            //     if (!S->reason[abs(*check)])                       //// If so, S->assigned is the first-UIP
            //         goto build;
            while (S->reason[abs(*(--check))])                        // An identical process
                if (S->fals[*check]==MARK) break;
            if (!S->reason[abs(*(check))] && S->fals[*(check)]!=MARK) break;
            clause = S->DB + S->reason[abs(*S->assigned)];            //// Spread the MARK all the other literals in the reason[*S->assighed]
            while (*(++clause))
                bump(S, *clause);
        }                                                             // MARK all literals in reason
        unassign(S, *S->assigned);                                    // Unassign the tail of the stack
    }
build:                                                                //// Now S->assigned pointed at the first-UIP (may be decision) (not unassigned)
                                                                      //// MARKs form an initial conflict clause
    int *p = S->processed = S->assigned;                              //// Since the first-UIP must be propagated, now S->processed > S->assigned
    int size = 0, lbd = 0, flag = 0;                                  // Build conflict clause; Empty the clause buffer
    while (p >= S->forced)
    {                                                                 // Only literals on the stack can be MARKed
        if ((S->fals[*p] == MARK) && !implied(S, *p))
        {                                                             // If MARKed and not implied
            S->buffer[size++] = *p;
            flag = 1;
        }                                                             // Add literal to conflict clause buffer
        if (!S->reason[abs(*p)])
        {
            lbd += flag;
            flag = 0;                                                 // Increase LBD for a decision with a true flag
            if (size == 1) S->processed = p;                          //// Update the position for backtracking
        }                                                             // And update the processed pointer
        S->fals[*(p--)] = 1;
    }                                                                 // Reset the MARK flag for all variables on the stack
    S->fast -= S->fast >>  5;
    S->fast += lbd << 15;                                             // Update the fast moving average
    S->slow -= S->slow >> 15;
    S->slow += lbd <<  5;                                             // Update the slow moving average
    while (S->assigned > S->processed)                                //// Perform non-chronological backtracking as described
        unassign(S, *(S->assigned--));
    unassign(S, *S->assigned);                                        // Assigned now equal to processed
    S->buffer[size] = 0;                                              // Terminate the buffer (and potentially print clause)
    return addClause(S, S->buffer, size, 0);                          // Add new conflict clause to redundant DB
}

int propagate(struct solver* S)                                       // Performs unit propagation
{
    bool forced = S->reason[abs(*S->processed)];                      //// Only if no decision was made might [forced] be true
    while (S->processed < S->assigned)
    {                                                                 // While unprocessed false literals
        int lit = *(S->processed++);                                  //// lit is falsified but not propagated yet
        int *watch = &S->first[lit];                                  //// Handle all the clauses watching lit (remove the associating watchers)
        while (*watch != END)
        {
            bool unit = true;
            int *clause = S->DB + *watch;	                          // Get the clause from DB
            if (clause[-1]==0) clause+=2;                             // Set the pointer to the first literal in the clause
            else clause++;
            if (clause[0]==lit) clause[0]=clause[1];                  // Ensure that the other watched literal is in front
            for (int i=2; unit&&clause[i]; i++)                       // Scan the non-watched literals
                if (!S->fals[clause[i]])
                {                                                     // When clause[i] is not false, it is either true or unset
                    unit = false;
                    clause[1] = clause[i];
					clause[i] = lit;                                  //// Finish swapping literals (keep the clause for backtracking)
                    int store = *watch;                               // Store the old watch
                    *watch = S->DB[*watch];                           // Remove the watch from the list of lit
                    addWatch(S, clause[1], store); 
                }                                                     // Add the watch to the list of clause[1]
            if (unit)
            {
                clause[1] = lit;                                      //// At most two literals,and lit is in clause[1]
                watch = (S->DB + *watch);                             // Place lit at clause[1] and update next watch
                if (S->fals[-clause[0]]) continue;                    // If the other watched literal is satisfied, continue
                else if (!S->fals[clause[0]])                         //// If the other watched literal is unassigned,
                    assign(S, clause, forced);                        //// the clause is unit for clause[0], thus assign
                else                                                  //// If the other watched literal is falsified,
                {                                                     //// a conflict is found by clause
                    if (forced) return UNSAT;                         // Found a root level conflict -> UNSAT
                    int* lemma = analyze(S, clause);                  // Analyze the conflict return a conflict clause
                    if (!lemma[1]) forced = true;                     //// In case the conflict clause is unit set forced flag
                    assign(S, lemma, forced);
                    break;
                }
            }
        }
    }                                                                 // Assign the conflict clause as a unit
    if (forced) S->forced = S->processed;	                          // Set S->forced if applicable
    return SAT;
}

int solve(struct solver* S)
{                                                                     // Determine satisfiability 
    int decision = S->head;
    for (;;)
    {                                                                 // Main solve loop
        int old_nLemmas = S->nLemmas;                                 // Store nLemmas to see whether propagate adds lemmas
        if (propagate(S) == UNSAT) return UNSAT;                      // Propagation returns UNSAT for a root level conflict
        if (S->nLemmas > old_nLemmas)
        {                                                             // If the last decision caused a conflict
            decision = S->head;                                       // Reset the decision heuristic to head
            if (S->fast > (S->slow / 100) * 125)
            {                                                         // If fast average is substantially larger than slow average
//				printf("c restarting after ** conflicts (%i %i) %i\n", S->fast, S->slow, S->nLemmas > S->maxLemmas);
                S->fast = (S->slow / 100) * 125;
                restart(S);                                           // Restart and update the averages
                if (S->nLemmas > S->maxLemmas)
					reduceDB(S, 6);                                   // Reduce the DB when it contains too many lemmas
            }
        }
        while (S->fals[decision] || S->fals[-decision])               // As long as the temporay decision is assigned
            decision = S->prev[decision];                             // Replace it with the next variable in the decision list
        if (decision == 0) return SAT;                                // If the end of the list is reached, then a solution is found
        decision = S->model[decision] ? decision : -decision;         // Otherwise, assign the decision variable based on the model
        S->fals[-decision] = 1;                                       // Assign the decision literal to true (change to IMPLIED-1?)
        *(S->assigned++) = -decision;                                 // And push it on the assigned stack
        decision = abs(decision);
        S->reason[decision] = 0;                                      // Decisions have no reason clauses
    }
}

void initCDCL(struct solver* S, int n, int m)
{
    if (n < 1)      n = 1;                                            // The code assumes that there is at least one variable
    S->nVars          = n;                                            // Set the number of variables
    S->nClauses       = m;                                            // Set the number of clauses
    S->mem_max        = 1 << 30;                                      // Set the initial maximum memory
    S->mem_used       = 0;                                            // The number of integers allocated in the DB
    S->nLemmas        = 0;                                            // The number of learned clauses -- redundant means learned
    S->nConflicts     = 0;                                            // Number of conflicts used to update scores
    S->maxLemmas      = 2000;                                         // Initial maximum number of learned clauses
    S->fast = S->slow = 1 << 24;                                      // Initialize the fast and slow moving averages

    S->DB = (int *) malloc (sizeof (int) * S->mem_max);               // Allocate the initial database
    S->model       = getMemory (S, n+1);                              // Full assignment of the (Boolean) variables (initially set to fals)
    S->next        = getMemory (S, n+1);                              // Next variable in the heuristic order
    S->prev        = getMemory (S, n+1);                              // Previous variable in the heuristic order
    S->buffer      = getMemory (S, n  );                              // A buffer to store a temporary clause
    S->reason      = getMemory (S, n+1);                              // Array of clauses
    S->falseStack  = getMemory (S, n+1);                              // Stack of falsified literals -- this pointer is never changed
    S->forced      = S->falseStack;                                   // Points inside *falseStack at first decision (unforced literal)
    S->processed   = S->falseStack;                                   // Points inside *falseStack at first unprocessed literal
    S->assigned    = S->falseStack;                                   // Points inside *falseStack at last unprocessed literal
    S->fals        = getMemory (S, 2*n+1); S->fals += n;              // Labels for variables, non-zero means false 
    S->first       = getMemory (S, 2*n+1); S->first += n;             // Offset of the first watched clause （链式栈的栈顶标志）
    S->DB[S->mem_used++] = 0;                                         // Make sure there is a 0 before the clauses are loaded.

    for (int i = 1; i <= n; i++)
    {                                                                 // Initialize the main datastructures:
        S->prev[i] = i-1;
        S->next[i-1] = i;                                             // the double-linked list for variable-move-to-front,
        S->model[i] = S->fals[-i] = S->fals[i] = 0;                   // the model (phase-saving), the fals array,
        S->first[i] = S->first[-i] = END;
    }                                                                 // and first (watch pointers).
	S->head = n;                                                      // Initialize the head of the double-linked list
}

static void read_until_new_line (FILE * input)
{
    int ch;
    while ((ch = getc (input)) != '\n')
        if (ch == EOF)
        {
            printf ("parse error: unexpected EOF");
            exit (1);
        }
}

int parse (struct solver* S, char* filename)
{                                                                          // Parse the formula and initialize
    int tmp;
    FILE* input = fopen (filename, "r");                                   // Read the CNF file
    while ((tmp = getc (input)) == 'c')
        read_until_new_line (input);
    ungetc (tmp, input);
    do
    {
        tmp = fscanf (input," p cnf %i %i \n", &S->nVars, &S->nClauses);   // Find the first non-comment line
        if (tmp > 0 && tmp != EOF) 
            break;
        tmp = fscanf (input, "%*s\n");
    } while (tmp != 2 && tmp != EOF);                                      // Skip it and read next line
    initCDCL(S, S->nVars, S->nClauses);                                    // Allocate the main datastructures
    int nZeros = S->nClauses, size = 0;                                    // Initialize the number of clauses to read
    while (nZeros > 0)
    {                                                                      // While there are clauses in the file
        int ch = getc (input);
        if (ch == ' ' || ch == '\n') continue;
        if (ch == 'c')
        {
            read_until_new_line (input);
            continue;
        }
        ungetc (ch, input);
        int lit = 0;
        tmp = fscanf (input, " %i ", &lit);                                // Read a literal.
        if (!lit)
        {                                                                  // If reaching the end of the clause
            int* clause = addClause(S, S->buffer, size, 1);                // Then add the clause to data_base
            if (!size || ((size == 1) && S->fals[clause[0]]))              // Check for empty clause or conflicting unit
                return UNSAT;                                              // If either is found return UNSAT
            if ((size == 1) && !S->fals[-clause[0]])                       // Check for a new unit
                assign (S, clause, 1);                                     // Directly assign new units (forced = 1)                
            size = 0;
            --nZeros;
        }                                                                  // Reset buffer
        else S->buffer[size++] = lit;
    }                                                                      // Add literal to buffer
    fclose (input);                                                        // Close the formula file
    return SAT;                                                            // Return that no conflict was observed
}

int main(int argc, char** argv)
{			                                                               // The main procedure for a STANDALONE solver
    struct solver S;	                                                   // Create the solver datastructure
    if (parse(&S, argv[1]) == UNSAT) printf("s UNSATISFIABLE\n");          // Parse the DIMACS file in argv[1]
    else if (solve(&S) == UNSAT) printf("s UNSATISFIABLE\n");              // Solve without limit (number of conflicts)
    else printf("s SATISFIABLE\n");                                        // And print whether the formula has a solution
    printf("c statistics of %s: mem: %i conflicts: %i max_lemmas: %i\n", argv[1], S.mem_used, S.nConflicts, S.maxLemmas);
}
