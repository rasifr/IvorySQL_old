/*-------------------------------------------------------------------------
 *
 * pl_handler.c		- Handler for the PL/iSQL
 *			  procedural language
 *
 * Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/pl/plisql/src/pl_handler.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "commands/packagecmds.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "plisql.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "utils/varlena.h"

static bool plisql_extra_checks_check_hook(char **newvalue, void **extra, GucSource source);
static void plisql_extra_warnings_assign_hook(const char *newvalue, void *extra);
static void plisql_extra_errors_assign_hook(const char *newvalue, void *extra);
static Oid	package_by_funcid(Oid funcOid);
static bool is_package_func(Oid funcOid, Oid *pkgOid);

PG_MODULE_MAGIC;

/* Custom GUC variable */
static const struct config_enum_entry variable_conflict_options[] = {
	{"error", PLISQL_RESOLVE_ERROR, false},
	{"use_variable", PLISQL_RESOLVE_VARIABLE, false},
	{"use_column", PLISQL_RESOLVE_COLUMN, false},
	{NULL, 0, false}
};

int			plisql_variable_conflict = PLISQL_RESOLVE_ERROR;

bool		plisql_print_strict_params = false;

bool		plisql_check_asserts = true;

char	   *plisql_extra_warnings_string = NULL;
char	   *plisql_extra_errors_string = NULL;
int			plisql_extra_warnings;
int			plisql_extra_errors;
PLiSQL_package *curr_pkg = NULL;

/* Hook for plugins */
PLiSQL_plugin **plisql_plugin_ptr = NULL;


static bool
plisql_extra_checks_check_hook(char **newvalue, void **extra, GucSource source)
{
	char	   *rawstring;
	List	   *elemlist;
	ListCell   *l;
	int			extrachecks = 0;
	int		   *myextra;

	if (pg_strcasecmp(*newvalue, "all") == 0)
		extrachecks = PLISQL_XCHECK_ALL;
	else if (pg_strcasecmp(*newvalue, "none") == 0)
		extrachecks = PLISQL_XCHECK_NONE;
	else
	{
		/* Need a modifiable copy of string */
		rawstring = pstrdup(*newvalue);

		/* Parse string into list of identifiers */
		if (!SplitIdentifierString(rawstring, ',', &elemlist))
		{
			/* syntax error in list */
			GUC_check_errdetail("List syntax is invalid.");
			pfree(rawstring);
			list_free(elemlist);
			return false;
		}

		foreach(l, elemlist)
		{
			char	   *tok = (char *) lfirst(l);

			if (pg_strcasecmp(tok, "shadowed_variables") == 0)
				extrachecks |= PLISQL_XCHECK_SHADOWVAR;
			else if (pg_strcasecmp(tok, "too_many_rows") == 0)
				extrachecks |= PLISQL_XCHECK_TOOMANYROWS;
			else if (pg_strcasecmp(tok, "strict_multi_assignment") == 0)
				extrachecks |= PLISQL_XCHECK_STRICTMULTIASSIGNMENT;
			else if (pg_strcasecmp(tok, "all") == 0 || pg_strcasecmp(tok, "none") == 0)
			{
				GUC_check_errdetail("Key word \"%s\" cannot be combined with other key words.", tok);
				pfree(rawstring);
				list_free(elemlist);
				return false;
			}
			else
			{
				GUC_check_errdetail("Unrecognized key word: \"%s\".", tok);
				pfree(rawstring);
				list_free(elemlist);
				return false;
			}
		}

		pfree(rawstring);
		list_free(elemlist);
	}

	myextra = (int *) malloc(sizeof(int));
	if (!myextra)
		return false;
	*myextra = extrachecks;
	*extra = (void *) myextra;

	return true;
}

static void
plisql_extra_warnings_assign_hook(const char *newvalue, void *extra)
{
	plisql_extra_warnings = *((int *) extra);
}

static void
plisql_extra_errors_assign_hook(const char *newvalue, void *extra)
{
	plisql_extra_errors = *((int *) extra);
}


/*
 * _PG_init()			- library load-time initialization
 *
 * DO NOT make this static nor change its name!
 */
void
_PG_init(void)
{
	/* Be sure we do initialization only once (should be redundant now) */
	static bool inited = false;
	remove_package_state *remove_pkgstate;

	if (inited)
		return;

	pg_bindtextdomain(TEXTDOMAIN);

	DefineCustomEnumVariable("plisql.variable_conflict",
							 gettext_noop("Sets handling of conflicts between PL/iSQL variable names and table column names."),
							 NULL,
							 &plisql_variable_conflict,
							 PLISQL_RESOLVE_ERROR,
							 variable_conflict_options,
							 PGC_SUSET, 0,
							 NULL, NULL, NULL);

	DefineCustomBoolVariable("plisql.print_strict_params",
							 gettext_noop("Print information about parameters in the DETAIL part of the error messages generated on INTO ... STRICT failures."),
							 NULL,
							 &plisql_print_strict_params,
							 false,
							 PGC_USERSET, 0,
							 NULL, NULL, NULL);

	DefineCustomBoolVariable("plisql.check_asserts",
							 gettext_noop("Perform checks given in ASSERT statements."),
							 NULL,
							 &plisql_check_asserts,
							 true,
							 PGC_USERSET, 0,
							 NULL, NULL, NULL);

	DefineCustomStringVariable("plisql.extra_warnings",
							   gettext_noop("List of programming constructs that should produce a warning."),
							   NULL,
							   &plisql_extra_warnings_string,
							   "none",
							   PGC_USERSET, GUC_LIST_INPUT,
							   plisql_extra_checks_check_hook,
							   plisql_extra_warnings_assign_hook,
							   NULL);

	DefineCustomStringVariable("plisql.extra_errors",
							   gettext_noop("List of programming constructs that should produce an error."),
							   NULL,
							   &plisql_extra_errors_string,
							   "none",
							   PGC_USERSET, GUC_LIST_INPUT,
							   plisql_extra_checks_check_hook,
							   plisql_extra_errors_assign_hook,
							   NULL);

	EmitWarningsOnPlaceholders("plisql");

	plisql_HashTableInit();
	package_HashTableInit();
	RegisterXactCallback(plisql_xact_cb, NULL);
	RegisterSubXactCallback(plisql_subxact_cb, NULL);

	/* Set up a rendezvous point with optional instrumentation plugin */
	plisql_plugin_ptr = (PLiSQL_plugin **) find_rendezvous_variable("PLiSQL_plugin");
	remove_pkgstate = (remove_package_state *)
						find_rendezvous_variable("remove_package_state");
	*remove_pkgstate = &delete_package_state_oid;

	inited = true;
}

/* ----------
 * plisql_call_handler
 *
 * The PostgreSQL function manager and trigger manager
 * call this function for execution of PL/iSQL procedures.
 * ----------
 */
PG_FUNCTION_INFO_V1(plisql_call_handler);

Datum
plisql_call_handler(PG_FUNCTION_ARGS)
{
	bool		nonatomic;
	PLiSQL_function *func = NULL;
	PLiSQL_execstate *save_cur_estate;
	ResourceOwner procedure_resowner;
	volatile Datum retval = (Datum) 0;
	int			rc;
	Oid			pkgOid;

	nonatomic = fcinfo->context &&
		IsA(fcinfo->context, CallContext) &&
		!castNode(CallContext, fcinfo->context)->atomic;

	/*
	 * Connect to SPI manager
	 */
	if ((rc = SPI_connect_ext(nonatomic ? SPI_OPT_NONATOMIC : 0)) != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed: %s", SPI_result_code_string(rc));

	if (is_package_func(fcinfo->flinfo->fn_oid, &pkgOid))
	{
		PLiSQL_package *pkg;

		pkg = plisql_compile_package(pkgOid, false);

		/* Find or compile the function */
		func = plisql_compile(fcinfo, false);
		plisql_package_exec_init(pkg, nonatomic);

		curr_pkg = NULL;
	}
	else
	{
		/* Find or compile the function */
		func = plisql_compile(fcinfo, false);
	}

	/* Must save and restore prior value of cur_estate */
	save_cur_estate = func->cur_estate;

	/* Mark the function as busy, so it can't be deleted from under us */
	func->use_count++;

	/*
	 * If we'll need a procedure-lifespan resowner to execute any CALL or DO
	 * statements, create it now.  Since this resowner is not tied to any
	 * parent, failing to free it would result in process-lifespan leaks.
	 * Therefore, be very wary of adding any code between here and the PG_TRY
	 * block.
	 */
	procedure_resowner =
		(nonatomic && func->requires_procedure_resowner) ?
		ResourceOwnerCreate(NULL, "PL/iSQL procedure resources") : NULL;

	PG_TRY();
	{
		/*
		 * Determine if called as function or trigger and call appropriate
		 * subhandler
		 */
		if (CALLED_AS_TRIGGER(fcinfo))
			retval = PointerGetDatum(plisql_exec_trigger(func,
														  (TriggerData *) fcinfo->context));
		else if (CALLED_AS_EVENT_TRIGGER(fcinfo))
		{
			plisql_exec_event_trigger(func,
									   (EventTriggerData *) fcinfo->context);
			/* there's no return value in this case */
		}
		else
			retval = plisql_exec_function(func, fcinfo,
										   NULL, NULL,
										   procedure_resowner,
										   !nonatomic);
	}
	PG_FINALLY();
	{
		/* Decrement use-count, restore cur_estate */
		func->use_count--;
		func->cur_estate = save_cur_estate;

		/* Be sure to release the procedure resowner if any */
		if (procedure_resowner)
		{
			ResourceOwnerReleaseAllPlanCacheRefs(procedure_resowner);
			ResourceOwnerDelete(procedure_resowner);
		}
	}
	PG_END_TRY();

	/*
	 * Disconnect from SPI manager
	 */
	if ((rc = SPI_finish()) != SPI_OK_FINISH)
		elog(ERROR, "SPI_finish failed: %s", SPI_result_code_string(rc));

	return retval;
}

/* ----------
 * plisql_inline_handler
 *
 * Called by PostgreSQL to execute an anonymous code block
 * ----------
 */
PG_FUNCTION_INFO_V1(plisql_inline_handler);

Datum
plisql_inline_handler(PG_FUNCTION_ARGS)
{
	LOCAL_FCINFO(fake_fcinfo, 0);
	InlineCodeBlock *codeblock = castNode(InlineCodeBlock, DatumGetPointer(PG_GETARG_DATUM(0)));
	PLiSQL_function *func;
	FmgrInfo	flinfo;
	EState	   *simple_eval_estate;
	ResourceOwner simple_eval_resowner;
	Datum		retval;
	int			rc;

	/*
	 * Connect to SPI manager
	 */
	if ((rc = SPI_connect_ext(codeblock->atomic ? 0 : SPI_OPT_NONATOMIC)) != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed: %s", SPI_result_code_string(rc));

	/* Compile the anonymous code block */
	func = plisql_compile_inline(codeblock->source_text);

	/* Mark the function as busy, just pro forma */
	func->use_count++;

	/*
	 * Set up a fake fcinfo with just enough info to satisfy
	 * plisql_exec_function().  In particular note that this sets things up
	 * with no arguments passed.
	 */
	MemSet(fake_fcinfo, 0, SizeForFunctionCallInfo(0));
	MemSet(&flinfo, 0, sizeof(flinfo));
	fake_fcinfo->flinfo = &flinfo;
	flinfo.fn_oid = InvalidOid;
	flinfo.fn_mcxt = CurrentMemoryContext;

	/*
	 * Create a private EState and resowner for simple-expression execution.
	 * Notice that these are NOT tied to transaction-level resources; they
	 * must survive any COMMIT/ROLLBACK the DO block executes, since we will
	 * unconditionally try to clean them up below.  (Hence, be wary of adding
	 * anything that could fail between here and the PG_TRY block.)  See the
	 * comments for shared_simple_eval_estate.
	 *
	 * Because this resowner isn't tied to the calling transaction, we can
	 * also use it as the "procedure" resowner for any CALL statements.  That
	 * helps reduce the opportunities for failure here.
	 */
	simple_eval_estate = CreateExecutorState();
	simple_eval_resowner =
		ResourceOwnerCreate(NULL, "PL/iSQL DO block simple expressions");

	/* And run the function */
	PG_TRY();
	{
		retval = plisql_exec_function(func, fake_fcinfo,
									   simple_eval_estate,
									   simple_eval_resowner,
									   simple_eval_resowner,	/* see above */
									   codeblock->atomic);
	}
	PG_CATCH();
	{
		/*
		 * We need to clean up what would otherwise be long-lived resources
		 * accumulated by the failed DO block, principally cached plans for
		 * statements (which can be flushed by plisql_free_function_memory),
		 * execution trees for simple expressions, which are in the private
		 * EState, and cached-plan refcounts held by the private resowner.
		 *
		 * Before releasing the private EState, we must clean up any
		 * simple_econtext_stack entries pointing into it, which we can do by
		 * invoking the subxact callback.  (It will be called again later if
		 * some outer control level does a subtransaction abort, but no harm
		 * is done.)  We cheat a bit knowing that plisql_subxact_cb does not
		 * pay attention to its parentSubid argument.
		 */
		plisql_subxact_cb(SUBXACT_EVENT_ABORT_SUB,
						   GetCurrentSubTransactionId(),
						   0, NULL);

		/* Clean up the private EState and resowner */
		FreeExecutorState(simple_eval_estate);
		ResourceOwnerReleaseAllPlanCacheRefs(simple_eval_resowner);
		ResourceOwnerDelete(simple_eval_resowner);

		/* Function should now have no remaining use-counts ... */
		func->use_count--;
		Assert(func->use_count == 0);

		/* ... so we can free subsidiary storage */
		plisql_free_function_memory(func);

		/* And propagate the error */
		PG_RE_THROW();
	}
	PG_END_TRY();

	/* Clean up the private EState and resowner */
	FreeExecutorState(simple_eval_estate);
	ResourceOwnerReleaseAllPlanCacheRefs(simple_eval_resowner);
	ResourceOwnerDelete(simple_eval_resowner);

	/* Function should now have no remaining use-counts ... */
	func->use_count--;
	Assert(func->use_count == 0);

	/* ... so we can free subsidiary storage */
	plisql_free_function_memory(func);

	/*
	 * Disconnect from SPI manager
	 */
	if ((rc = SPI_finish()) != SPI_OK_FINISH)
		elog(ERROR, "SPI_finish failed: %s", SPI_result_code_string(rc));

	return retval;
}

/* ----------
 * plisql_validator
 *
 * This function attempts to validate a PL/iSQL function at
 * CREATE FUNCTION time.
 * ----------
 */
PG_FUNCTION_INFO_V1(plisql_validator);

Datum
plisql_validator(PG_FUNCTION_ARGS)
{
	Oid			funcoid = PG_GETARG_OID(0);
	HeapTuple	tuple;
	Form_pg_proc proc;
	char		functyptype;
	int			numargs;
	Oid		   *argtypes;
	char	  **argnames;
	char	   *argmodes;
	bool		is_dml_trigger = false;
	bool		is_event_trigger = false;
	int			i;

	if (!CheckFunctionValidatorAccess(fcinfo->flinfo->fn_oid, funcoid))
		PG_RETURN_VOID();

	/* Get the new function's pg_proc entry */
	tuple = SearchSysCache1(PROCOID, ObjectIdGetDatum(funcoid));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for function %u", funcoid);
	proc = (Form_pg_proc) GETSTRUCT(tuple);

	functyptype = get_typtype(proc->prorettype);

	/* Disallow pseudotype result */
	/* except for TRIGGER, EVTTRIGGER, RECORD, VOID, or polymorphic */
	if (functyptype == TYPTYPE_PSEUDO)
	{
		if (proc->prorettype == TRIGGEROID)
			is_dml_trigger = true;
		else if (proc->prorettype == EVENT_TRIGGEROID)
			is_event_trigger = true;
		else if (proc->prorettype != RECORDOID &&
				 proc->prorettype != VOIDOID &&
				 !IsPolymorphicType(proc->prorettype))
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("PL/iSQL functions cannot return type %s",
							format_type_be(proc->prorettype))));
	}

	/* Disallow pseudotypes in arguments (either IN or OUT) */
	/* except for RECORD and polymorphic */
	numargs = get_func_arg_info(tuple,
								&argtypes, &argnames, &argmodes);
	for (i = 0; i < numargs; i++)
	{
		if (get_typtype(argtypes[i]) == TYPTYPE_PSEUDO)
		{
			if (argtypes[i] != RECORDOID &&
				!IsPolymorphicType(argtypes[i]))
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("PL/iSQL functions cannot accept type %s",
								format_type_be(argtypes[i]))));
		}
	}

	/* Postpone body checks if !check_function_bodies */
	if (check_function_bodies)
	{
		LOCAL_FCINFO(fake_fcinfo, 0);
		FmgrInfo	flinfo;
		int			rc;
		TriggerData trigdata;
		EventTriggerData etrigdata;
		Oid			pkgOid;

		/*
		 * Connect to SPI manager (is this needed for compilation?)
		 */
		if ((rc = SPI_connect()) != SPI_OK_CONNECT)
			elog(ERROR, "SPI_connect failed: %s", SPI_result_code_string(rc));

		/*
		 * Set up a fake fcinfo with just enough info to satisfy
		 * plisql_compile().
		 */
		MemSet(fake_fcinfo, 0, SizeForFunctionCallInfo(0));
		MemSet(&flinfo, 0, sizeof(flinfo));
		fake_fcinfo->flinfo = &flinfo;
		flinfo.fn_oid = funcoid;
		flinfo.fn_mcxt = CurrentMemoryContext;
		if (is_dml_trigger)
		{
			MemSet(&trigdata, 0, sizeof(trigdata));
			trigdata.type = T_TriggerData;
			fake_fcinfo->context = (Node *) &trigdata;
		}
		else if (is_event_trigger)
		{
			MemSet(&etrigdata, 0, sizeof(etrigdata));
			etrigdata.type = T_EventTriggerData;
			fake_fcinfo->context = (Node *) &etrigdata;
		}

		/* Test-compile the function */
		if (is_package_func(fake_fcinfo->flinfo->fn_oid, &pkgOid))
		{
			plisql_compile_package(pkgOid, true);
			curr_pkg = NULL;
		}
		else
		{
			plisql_compile(fake_fcinfo, true);
		}

		/*
		 * Disconnect from SPI manager
		 */
		if ((rc = SPI_finish()) != SPI_OK_FINISH)
			elog(ERROR, "SPI_finish failed: %s", SPI_result_code_string(rc));
	}

	ReleaseSysCache(tuple);

	PG_RETURN_VOID();
}

/*
 * See if the given function is member of a package.
 *
 * returns the package oid of the function if its a memeber of package. InvalidOid
 * otherwise.
 */
static Oid
package_by_funcid(Oid funcOid)
{
	Oid			pkgOid = 0;
	Form_pg_proc proc;
	HeapTuple	tuple;

	tuple = SearchSysCache1(PROCOID, ObjectIdGetDatum(funcOid));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for function %u", funcOid);

	proc = (Form_pg_proc) GETSTRUCT(tuple);
	if (OidIsValid(proc->pronamespace))
	{
		/* validate if package exists */
		if (get_package_name(proc->pronamespace, true) == NULL)
			pkgOid = InvalidOid;
		else
			pkgOid = proc->pronamespace;
	}

	ReleaseSysCache(tuple);
	return pkgOid;
}

/*
 * returns true if the given function is package function. false otherwise.
 */
static bool
is_package_func(Oid funcOid, Oid *pkgOid)
{
	Oid			oid = package_by_funcid(funcOid);

	if (pkgOid)
		*pkgOid = oid;
	return OidIsValid(oid);
}
