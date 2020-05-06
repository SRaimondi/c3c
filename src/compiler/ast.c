// Copyright (c) 2019 Christoffer Lerno. All rights reserved.
// Use of this source code is governed by the GNU LGPLv3.0 license
// a copy of which can be found in the LICENSE file.

#include "compiler_internal.h"

static void fprint_asts_recursive(FILE *file, Ast **asts, int indent);
static void fprint_decl_list(FILE *file, Decl **decls, int indent);
static void fprint_ast_recursive(FILE *file, Ast *ast, int indent);

#define DUMP(text) do { fprintf_indented(file, indent, text); fprintf(file, "\n"); } while(0)
#define DUMPF(text, ...) do { fprintf_indented(file, indent, text, __VA_ARGS__); fprintf(file, "\n"); } while(0)
#define DUMPI(text) do { fprintf_indented(file, indent + 1, text); fprintf(file, "\n"); } while(0)
#define DUMPFI(text, ...) do { fprintf_indented(file, indent + 1, text, __VA_ARGS__); fprintf(file, "\n"); } while(0)
#define DUMPE() fprint_endparen(file, indent);
#define DUMPEND() fprint_endparen(file, indent); return
#define DUMPEXPR(_expr) fprint_expr_recursive(file, _expr, indent + 1)
#define DUMPAST(_ast) fprint_ast_recursive(file, _ast, indent + 1)
#define DUMPTI(_type_info) fprint_type_info_recursive(file, _type_info, indent + 1)
#define DUMPTYPE(_type) fprint_type_recursive(file, _type, indent + 1)
#define DUMPDECLS(_decls) fprint_decl_list(file, _decls, indent + 1)

Decl *decl_new(DeclKind decl_kind, Token name, Visibility visibility)
{
	Decl *decl = CALLOCS(Decl);
	decl->decl_kind = decl_kind;
	decl->name_span = name.span;
	decl->span = name.span;
	decl->name = name.string;
	decl->visibility = visibility;
	return decl;
}


static Type poison_type = { .type_kind = TYPE_POISONED };
static TypeInfo poison_type_info = { .kind = TYPE_INFO_POISON };
Type *poisoned_type = &poison_type;
TypeInfo *poisoned_type_info = &poison_type_info;

void decl_set_external_name(Decl *decl)
{
	if (decl->visibility == VISIBLE_EXTERN)
	{
		decl->external_name = decl->name;
		return;
	}
	char buffer[1024];
	uint32_t len = sprintf(buffer, "%s::%s", decl->module->name->module, decl->name);
	assert(len);
	TokenType type = TOKEN_INVALID_TOKEN;
	decl->external_name = symtab_add(buffer, len, fnv1a(buffer, len), &type);
}

Decl *decl_new_with_type(Token name, DeclKind decl_type, Visibility visibility)
{
	Decl *decl = decl_new(decl_type, name, visibility);
	TypeKind kind = TYPE_POISONED;
	switch (decl_type)
	{
		case DECL_FUNC:
			kind = TYPE_FUNC;
			break;
		case DECL_UNION:
			kind = TYPE_UNION;
			break;
		case DECL_STRUCT:
			kind = TYPE_STRUCT;
			break;
		case DECL_ERROR:
			kind = TYPE_ERROR;
			break;
		case DECL_ENUM:
			kind = TYPE_ENUM;
			break;
		case DECL_TYPEDEF:
			kind = TYPE_TYPEDEF;
			break;
		case DECL_THROWS:
		case DECL_POISONED:
		case DECL_VAR:
		case DECL_ENUM_CONSTANT:
		case DECL_ERROR_CONSTANT:
		case DECL_ARRAY_VALUE:
		case DECL_IMPORT:
		case DECL_MACRO:
		case DECL_GENERIC:
		case DECL_CT_IF:
		case DECL_CT_ELSE:
		case DECL_CT_ELIF:
		case DECL_ATTRIBUTE:
			UNREACHABLE
	}
	Type *type = type_new(kind, name.string);
	type->canonical = type;
	type->decl = decl;
	decl->type = type;
	return decl;
}

const char *decl_var_to_string(VarDeclKind kind)
{
	switch (kind)
	{
		case VARDECL_CONST:
			return "const";
		case VARDECL_GLOBAL:
			return "global";
		case VARDECL_LOCAL:
			return "local";
		case VARDECL_PARAM:
			return "param";
		case VARDECL_MEMBER:
			return "member";
	}
	UNREACHABLE
}

static Decl poison_decl = { .decl_kind = DECL_POISONED, .resolve_status = RESOLVE_DONE };
Decl *poisoned_decl = &poison_decl;

Decl *decl_new_var(Token name, TypeInfo *type, VarDeclKind kind, Visibility visibility)
{
	Decl *decl = decl_new(DECL_VAR, name, visibility);
	decl->var.kind = kind;
	decl->var.type_info = type;
	return decl;
}

/**
 * Recursively find a node in a declaration.
 * @return NULL if it wasn't found, otherwise the member.
 */
Decl *struct_find_name(Decl *decl, const char* name)
{
    Decl** compare_members = decl->strukt.members;
    VECEACH(compare_members, i)
    {
        Decl *member = compare_members[i];
        if (!member->name)
        {
            Decl *found = struct_find_name(member, name);
            if (found) return found;
        }
        else if (member->name == name) return member;
    }
    return NULL;
}

Ast *ast_from_expr(Expr *expr)
{
	if (!expr_ok(expr)) return poisoned_ast;
	Ast *ast = AST_NEW(AST_EXPR_STMT, expr->span);
	ast->expr_stmt = expr;
	return ast;
}

Expr *expr_new(ExprKind kind, SourceRange start)
{
	Expr *expr = malloc_arena(sizeof(Expr));
	expr->expr_kind = kind;
	expr->span = start;
	expr->type = NULL;
	return expr;
}

static Expr poison_expr = { .expr_kind = EXPR_POISONED, .resolve_status = RESOLVE_DONE };
Expr *poisoned_expr = &poison_expr;

Type* type_int_max_type(bool is_signed)
{
	return is_signed ? type_long : type_ulong;
}

/*
Type* type_get_unsigned(Type *type)
{
	assert(type->type_kind == TYPE_BUILTIN);
	Decl *decl = type->decl;
	if (decl->builtin.num_type == NUMBER_TYPE_UNSIGNED_INT) return type;
	assert(decl->builtin.num_type == NUMBER_TYPE_SIGNED_INT);
	switch (decl->builtin.bytes)
	{
		case 8:
			return &type_ulong;
		case 4:
			return &type_uint;
		case 2:
			return &type_ushort;
		case 1:
			return &type_byte;
		default:
			UNREACHABLE
	}
}

*/



BinaryOp binary_op[TOKEN_LAST + 1] = {
		[TOKEN_STAR] = BINARYOP_MULT,
		[TOKEN_MULT_MOD] = BINARYOP_MULT_MOD,
		[TOKEN_DIV] = BINARYOP_DIV,
		[TOKEN_PLUS] = BINARYOP_ADD,
		[TOKEN_PLUS_MOD] = BINARYOP_ADD_MOD,
		[TOKEN_MINUS] = BINARYOP_SUB,
		[TOKEN_MINUS_MOD] = BINARYOP_SUB_MOD,
		[TOKEN_MOD] = BINARYOP_MOD,
		[TOKEN_SHL] = BINARYOP_SHL,
		[TOKEN_SHR] = BINARYOP_SHR,
		[TOKEN_AND] = BINARYOP_AND,
		[TOKEN_OR] = BINARYOP_OR,
		[TOKEN_AMP] = BINARYOP_BIT_AND,
		[TOKEN_BIT_OR] = BINARYOP_BIT_OR,
		[TOKEN_BIT_XOR] = BINARYOP_BIT_XOR,
		[TOKEN_EQEQ] = BINARYOP_EQ,
		[TOKEN_NOT_EQUAL] = BINARYOP_NE,
		[TOKEN_LESS] = BINARYOP_LT,
		[TOKEN_LESS_EQ] = BINARYOP_LE,
		[TOKEN_GREATER] = BINARYOP_GT,
		[TOKEN_GREATER_EQ] = BINARYOP_GE,
		[TOKEN_EQ] = BINARYOP_ASSIGN,
		[TOKEN_MULT_ASSIGN] = BINARYOP_MULT_ASSIGN,
		[TOKEN_MULT_MOD_ASSIGN] = BINARYOP_MULT_MOD_ASSIGN,
		[TOKEN_PLUS_ASSIGN] = BINARYOP_ADD_ASSIGN,
		[TOKEN_PLUS_MOD_ASSIGN] = BINARYOP_ADD_MOD_ASSIGN,
		[TOKEN_MINUS_ASSIGN] = BINARYOP_SUB_ASSIGN,
		[TOKEN_MINUS_MOD_ASSIGN] = BINARYOP_SUB_MOD_ASSIGN,
		[TOKEN_DIV_ASSIGN] = BINARYOP_DIV_ASSIGN,
		[TOKEN_MOD_ASSIGN] = BINARYOP_MOD_ASSIGN,
		[TOKEN_BIT_AND_ASSIGN] = BINARYOP_BIT_AND_ASSIGN,
		[TOKEN_BIT_OR_ASSIGN] = BINARYOP_BIT_OR_ASSIGN,
		[TOKEN_BIT_XOR_ASSIGN] = BINARYOP_BIT_XOR_ASSIGN,
		[TOKEN_SHR_ASSIGN] = BINARYOP_SHR_ASSIGN,
		[TOKEN_SHL_ASSIGN] = BINARYOP_SHL_ASSIGN,
};


static BinaryOp assign_binop[BINARYOP_LAST + 1] = {
		[BINARYOP_MULT_ASSIGN] = BINARYOP_MULT,
		[BINARYOP_MULT_MOD_ASSIGN] = BINARYOP_MULT_MOD,
		[BINARYOP_ADD_ASSIGN] = BINARYOP_ADD,
		[BINARYOP_ADD_MOD_ASSIGN] = BINARYOP_ADD_MOD,
		[BINARYOP_SUB_ASSIGN] = BINARYOP_SUB,
		[BINARYOP_SUB_MOD_ASSIGN] = BINARYOP_SUB_MOD,
		[BINARYOP_DIV_ASSIGN] = BINARYOP_DIV,
		[BINARYOP_MOD_ASSIGN] = BINARYOP_MOD,
		[BINARYOP_BIT_AND_ASSIGN] = BINARYOP_BIT_AND,
		[BINARYOP_BIT_OR_ASSIGN] = BINARYOP_BIT_OR,
		[BINARYOP_BIT_XOR_ASSIGN] = BINARYOP_BIT_XOR,
		[BINARYOP_SHR_ASSIGN] = BINARYOP_SHR,
		[BINARYOP_SHL_ASSIGN] = BINARYOP_SHL,
};

BinaryOp binaryop_assign_base_op(BinaryOp assign_binary_op)
{
	return assign_binop[(int)assign_binary_op];
}

UnaryOp unary_op[TOKEN_LAST + 1] = {
		[TOKEN_STAR] = UNARYOP_DEREF,
		[TOKEN_AMP] = UNARYOP_ADDR,
		[TOKEN_BIT_NOT] = UNARYOP_BITNEG,
		[TOKEN_NOT] = UNARYOP_NOT,
		[TOKEN_MINUS] = UNARYOP_NEG,
		[TOKEN_MINUS_MOD] = UNARYOP_NEGMOD,
		[TOKEN_PLUSPLUS] = UNARYOP_INC,
		[TOKEN_MINUSMINUS] = UNARYOP_DEC,
};


PostUnaryOp post_unary_op[TOKEN_LAST + 1] = {
		[TOKEN_PLUSPLUS] = POSTUNARYOP_INC,
		[TOKEN_MINUSMINUS] = POSTUNARYOP_DEC,
};



BinaryOp binaryop_from_token(TokenType type)
{
	return binary_op[type];
}

TokenType binaryop_to_token(BinaryOp type)
{
	for (unsigned i = 0; i <= TOKEN_LAST; i++)
	{
		if (binary_op[i] == type) return (TokenType)i;
	}
	return TOKEN_INVALID_TOKEN;
}

UnaryOp unaryop_from_token(TokenType type)
{
	return unary_op[type];
}

TokenType unaryop_to_token(UnaryOp type)
{
	for (unsigned i = 0; i <= TOKEN_LAST; i++)
	{
		if (unary_op[i] == type) return (TokenType)i;
	}
	return TOKEN_INVALID_TOKEN;
}

PostUnaryOp post_unaryop_from_token(TokenType type)
{
	return post_unary_op[type];
}

TokenType postunaryop_to_token(PostUnaryOp type)
{
	for (unsigned i = 0; i <= TOKEN_LAST; i++)
	{
		if (post_unary_op[i] == type) return (TokenType)i;
	}
	return TOKEN_INVALID_TOKEN;
}

static Ast poison_ast = { .ast_kind = AST_POISONED };
Ast *poisoned_ast = &poison_ast;

void fprint_indent(FILE *file, int indent)
{
	for (int i = 0; i < indent * 2; i++) fprintf(file, " ");
}

static void fprintf_indented(FILE *file, int indent, const char *string, ...)
{
	fprint_indent(file, indent);
	va_list list;
	va_start(list, string);
	vfprintf(file, string, list);
	va_end(list);
}

void fprint_endparen(FILE *file, int indent)
{
	fprint_indent(file, indent);
	fprintf(file, ")\n");
}

void fprint_decl_recursive(FILE *file, Decl *decl, int indent);

void fprint_type_recursive(FILE *file, Type *type, int indent)
{
	if (!type)
	{
		fprintf_indented(file, indent, "(none)\n");
		return;
	}
	switch (type->type_kind)
	{
		case TYPE_POISONED:
			fprintf_indented(file, indent, "(type poison)\n");
			return;
		case TYPE_TYPEID:
			DUMP("(typeid)");
			return;
		case TYPE_FUNC:
			fprintf_indented(file, indent, "(type-func %s)\n", type->func.signature->mangled_signature);
			return;
		case TYPE_STRUCT:
			fprintf_indented(file, indent, "(struct %s::%s)\n", type->decl->module->name, type->decl->name);
			return;
		case TYPE_UNION:
			fprintf_indented(file, indent, "(union %s::%s)\n", type->decl->module->name, type->decl->name);
			return;
		case TYPE_ENUM:
			fprintf_indented(file, indent, "(enum %s::%s)\n", type->decl->module->name, type->decl->name);
			return;
		case TYPE_ERROR:
			fprintf_indented(file, indent, "(error %s::%s)\n", type->decl->module->name, type->decl->name);
			return;
		case TYPE_TYPEDEF:
			DUMPF("(user-defined %s", type->name);
			DUMPTYPE(type->canonical);
			DUMPEND();
		case TYPE_POINTER:
			fprintf_indented(file, indent, "(pointer\n");
			fprint_type_recursive(file, type->pointer, indent + 1);
			fprint_endparen(file, indent);
			break;
		case TYPE_SUBARRAY:
			fprintf_indented(file, indent, "(subarray\n");
			fprint_type_recursive(file, type->array.base, indent + 1);
			fprint_endparen(file, indent);
			break;
		case TYPE_VARARRAY:
			fprintf_indented(file, indent, "(vararray\n");
			fprint_type_recursive(file, type->array.base, indent + 1);
			fprint_endparen(file, indent);
			break;
		case TYPE_ARRAY:
			fprintf_indented(file, indent, "(array [%zu]\n", type->array.len);
			fprint_type_recursive(file, type->array.base, indent + 1);
			fprint_endparen(file, indent);
			break;
		case TYPE_VOID:
		case TYPE_BOOL:
		case TYPE_I8:
		case TYPE_I16:
		case TYPE_I32:
		case TYPE_I64:
		case TYPE_U8:
		case TYPE_U16:
		case TYPE_U32:
		case TYPE_U64:
		case TYPE_F32:
		case TYPE_F64:
			fprintf_indented(file, indent, "(type %s)\n", type->name);
			break;
		case TYPE_IXX:
			fprintf_indented(file, indent, "(comp time int)\n");
			break;
		case TYPE_FXX:
			fprintf_indented(file, indent, "(comp time float)\n");
			break;
		case TYPE_STRING:
			DUMP("(string)");
			return;
		case TYPE_ERROR_UNION:
			DUMP("(error-union)");
			return;
	}
}

const char *resolve_status_to_string(ResolveStatus status)
{
	switch (status)
	{
		case RESOLVE_NOT_DONE:
			return "not_done";
		case RESOLVE_DONE:
			return "done";
		case RESOLVE_RUNNING:
			return "running";
		default:
			UNREACHABLE
	}
}


void fprint_type_info_recursive(FILE *file, TypeInfo *type_info, int indent)
{
	if (!type_info)
	{
		DUMP("(type_info missing)");
		return;
	}
	DUMP("(type_info");
	DUMPFI("(resolve_status %s)", resolve_status_to_string(type_info->resolve_status));
	if (type_info->resolve_status == RESOLVE_DONE)
	{
		DUMPTYPE(type_info->type);
		DUMPEND();
	}
	indent++;
	switch (type_info->kind)
	{
		case TYPE_INFO_POISON:
			DUMP("(POISON)");
			break;
		case TYPE_INFO_IDENTIFIER:
			if (type_info->unresolved.path)
			{
				DUMPF("(unresolved %s::%s)\n", type_info->unresolved.path->module, type_info->unresolved.name_loc.string);
				break;;
			}
			DUMPF("(unresolved %s)", type_info->unresolved.name_loc.string);
			break;
		case TYPE_INFO_ARRAY:
			DUMP("(unresolved-array");
			DUMPTI(type_info->array.base);
			DUMPEXPR(type_info->array.len);
			DUMPE();
			break;
		case TYPE_INFO_POINTER:
			DUMP("pointer");
			DUMPTI(type_info->pointer);
			DUMPE();
			break;
		case TYPE_INFO_INC_ARRAY:
			DUMP("(incarray");
			DUMPTI(type_info->array.base);
			DUMPE();
		case TYPE_INFO_EXPRESSION:
			DUMP("(typexpr");
			DUMPEXPR(type_info->unresolved_type_expr);
			DUMPE();
			break;
	}
	indent--;
	DUMPEND();
}

void fprint_expr_common(FILE *file, Expr *expr, int indent)
{
	switch (expr->resolve_status)
	{
		case RESOLVE_NOT_DONE:
			DUMP("(unresolved)");
			break;
		case RESOLVE_RUNNING:
			DUMP("(resolving)");
			break;
		case RESOLVE_DONE:
			fprint_type_recursive(file, expr->type, indent);
			break;
	}
}

#define DUMPEXPC(_expr) fprint_expr_common(file, _expr, indent + 1)

void fprint_expr_recursive(FILE *file, Expr *expr, int indent)
{
	if (!expr) return;
	switch (expr->expr_kind)
	{
		case EXPR_IDENTIFIER:
			DUMPF("(ident %s", expr->identifier_expr.identifier);
			DUMPEXPC(expr);
			DUMPEND();
		case EXPR_EXPR_BLOCK:
			if (!expr->expr_block.stmts)
			{
				DUMP("(expr_block)");
				return;
			}
			DUMP("(expr_block");
			fprint_asts_recursive(file, expr->expr_block.stmts, indent + 1);
			DUMPEND();
		case EXPR_CONST:
			fprintf_indented(file, indent, "(const ");
			expr_const_fprint(file, &expr->const_expr);
			fprintf(file, "\n");
			fprint_expr_common(file, expr, indent + 1);
			DUMPEND();
		case EXPR_BINARY:
			DUMPF("(binary %s", token_type_to_string(binaryop_to_token(expr->binary_expr.operator)));
			DUMPEXPC(expr);
			fprint_expr_recursive(file, expr->binary_expr.left, indent + 1);
			fprint_expr_recursive(file, expr->binary_expr.right, indent + 1);
			DUMPEND();
		case EXPR_UNARY:
			DUMPF("(unary %s", token_type_to_string(unaryop_to_token(expr->unary_expr.operator)));
			DUMPEXPC(expr);
			DUMPEXPR(expr->unary_expr.expr);
			DUMPEND();
		case EXPR_POST_UNARY:
			DUMPF("(postunary %s", token_type_to_string(postunaryop_to_token(expr->post_expr.operator)));
			DUMPEXPC(expr);
			DUMPEXPR(expr->post_expr.expr);
			DUMPEND();
		case EXPR_TYPE_ACCESS:
			DUMPF("(typeaccess .%s", expr->type_access.name.string);
			DUMPEXPC(expr);
			DUMPTI(expr->type_access.type);
			DUMPEND();
		case EXPR_ACCESS:
			DUMPF("(access .%s", expr->access_expr.sub_element.string);
			DUMPEXPC(expr);
			DUMPEXPR(expr->access_expr.parent);
			DUMPEND();
		case EXPR_TYPEID:
			DUMP("(typeid");
			DUMPEXPC(expr);
			DUMPTI(expr->typeid_expr);
			DUMPEND();
		case EXPR_GROUP:
			DUMP("(group");
			DUMPEXPR(expr->group_expr);
			DUMPEND();
		case EXPR_CALL:
			DUMP("(call");
			fprint_expr_common(file, expr, indent + 1);
			fprint_expr_recursive(file, expr->call_expr.function, indent + 1);
			VECEACH(expr->call_expr.arguments, i)
			{
				fprint_expr_recursive(file, expr->call_expr.arguments[i], indent + 1);
			}
			DUMPEND();
		case EXPR_TERNARY:
			if (!expr->ternary_expr.then_expr)
			{
				DUMP("(elvis");
				DUMPEXPC(expr);
				DUMPEXPR(expr->ternary_expr.cond);
			}
			else
			{
				DUMP("(cond");
				DUMPEXPC(expr);
				DUMPEXPR(expr->ternary_expr.cond);
				DUMPEXPR(expr->ternary_expr.then_expr);
			}
			DUMPEXPR(expr->ternary_expr.else_expr);
			DUMPEND();
		case EXPR_INITIALIZER_LIST:
			fprintf_indented(file, indent, "(initializerlist ");
			switch (expr->expr_initializer.init_type)
			{
				case INITIALIZER_UNKNOWN:

					fprintf(file, "not-analyzed\n");
					break;
				case INITIALIZER_ZERO:
					fprintf(file, "zero\n");
					break;
				case INITIALIZER_NORMAL:
					fprintf(file, "normal\n");
					break;
				case INITIALIZER_DESIGNATED:
					fprintf(file, "designated\n");
					break;
			}
			DUMPEXPC(expr);
			{
				VECEACH(expr->expr_initializer.initializer_expr, i)
				{
					fprint_expr_recursive(file, expr->expr_initializer.initializer_expr[i], indent + 1);
				}
			}
			DUMPEND();
		case EXPR_SUBSCRIPT:
			DUMP("(subscript");
			DUMPEXPC(expr);
			DUMPEXPR(expr->subscript_expr.expr);
			DUMPEXPC(expr->subscript_expr.index);
			DUMPEND();
		case EXPR_TRY:
			switch (expr->try_expr.type)
			{
				case TRY_EXPR_ELSE_JUMP:
					DUMP("(try-else-jump");
					DUMPEXPC(expr);
					DUMPEXPR(expr->try_expr.expr);
					DUMPAST(expr->try_expr.else_stmt);
					DUMPEND();
				case TRY_EXPR_ELSE_EXPR:
					DUMP("(try-else");
					DUMPEXPC(expr);
					DUMPEXPR(expr->try_expr.expr);
					DUMPEXPR(expr->try_expr.else_expr);
					DUMPEND();
				case TRY_STMT:
					DUMP("(try-stmt");
					DUMPAST(expr->try_expr.stmt);
					DUMPEND();
				case TRY_EXPR:
					DUMP("(try-expr");
					DUMPEXPR(expr->try_expr.expr);
					DUMPEND();
			}
			UNREACHABLE
		case EXPR_CAST:
			DUMP("(cast\n");
			DUMPEXPC(expr);
			DUMPTI(expr->cast_expr.type_info);
			DUMPEXPR(expr->cast_expr.expr);
			DUMPEND();
		case EXPR_EXPRESSION_LIST:
			DUMP("(expression-list");
			DUMPEXPC(expr);
			DUMPTI(expr->struct_value_expr.type);
			VECEACH(expr->expression_list, i)
			{
				fprint_expr_recursive(file, expr->expression_list[i], indent + 1);
			}
			DUMPEND();
		case EXPR_POISONED:
			DUMP("(POISONED)");
			return;
		case EXPR_TYPEOF:
			DUMP("(typeof");
			DUMPEXPR(expr->typeof_expr);
			DUMPEND();
		case EXPR_SCOPED_EXPR:
			DUMP("(scopedexpr");
			DUMPEXPR(expr->expr_scope.expr);
			// TODO defers.
			DUMPEND();
		case EXPR_MACRO_EXPR:
			DUMP("(macro-expr");
			DUMPEXPR(expr->macro_expr);
			DUMPEND();
		case EXPR_RANGE:
			DUMP("(range");
			DUMPEXPR(expr->range_expr.left);
			DUMPEXPR(expr->range_expr.right);
			DUMPEND();
		case EXPR_DESIGNATED_INITIALIZER:
			DUMP("(designated-initializer");
			// TODO path
			DUMPEXPR(expr->designated_init_expr.value);
			DUMPEND();
		case EXPR_COMPOUND_LITERAL:
			DUMP("(compound-literal");
			DUMPTI(expr->expr_compound_literal.type_info);
			DUMPEXPR(expr->expr_compound_literal.initializer);
			DUMPEND();
	}
	UNREACHABLE
}


void fprint_func_signature(FILE *file, FunctionSignature *signature, int indent)
{
	fprint_type_info_recursive(file, signature->rtype, indent);
	if (!vec_size(signature->params))
	{
		fprintf_indented(file, indent, "(params none)\n");
		return;
	}
	fprintf_indented(file, indent, "(params\n");
	fprint_decl_list(file, signature->params, indent + 1);
	fprint_endparen(file, indent);
	if (signature->throw_any)
	{
		fprintf_indented(file, indent, "(throws any)\n");
	}
	else
	{
		fprintf_indented(file, indent, "(throws\n");
		fprint_decl_list(file, signature->throws, indent + 1);
		fprint_endparen(file, indent);
	}
}
void fprint_decl_recursive(FILE *file, Decl *decl, int indent)
{
	switch (decl->decl_kind)
	{
		case DECL_VAR:
			DUMPF("(var-%s %s", decl_var_to_string(decl->var.kind), decl->name ?: "");
			DUMPTI(decl->var.type_info);
			switch (decl->var.kind)
			{
				case VARDECL_CONST:
					DUMPEXPR(decl->var.init_expr);
					break;
				case VARDECL_GLOBAL:
					DUMPEXPR(decl->var.init_expr);
					break;
				case VARDECL_LOCAL:
					DUMPEXPR(decl->var.init_expr);
					break;
				case VARDECL_PARAM:
					DUMPEXPR(decl->var.init_expr);
					break;
				case VARDECL_MEMBER:
					break;
			}
			DUMPEND();
		case DECL_MACRO:
			fprintf_indented(file, indent, "(macro %s\n", decl->name);
			fprint_type_info_recursive(file, decl->macro_decl.rtype, indent + 1);
			fprint_indent(file, indent + 1);
			fprintf(file, "(params\n");
			fprint_decl_list(file, decl->macro_decl.parameters, indent + 2);
			fprint_endparen(file, indent + 1);
			fprint_ast_recursive(file, decl->macro_decl.body, indent + 1);
			fprint_endparen(file, indent);
			break;
		case DECL_FUNC:
			fprintf_indented(file, indent, "(func %s\n", decl->name);
			if (decl->func.type_parent)
			{
				fprint_indent(file, indent + 1);
				fprintf(file, "(parent_type\n");
				fprint_type_info_recursive(file, decl->func.type_parent, indent + 2);
				fprint_indent(file, indent + 1);
				fprintf(file, ")\n");
			}
			fprint_func_signature(file, &decl->func.function_signature, indent + 1);
			if (decl->func.body) fprint_ast_recursive(file, decl->func.body, indent + 1);
			fprint_endparen(file, indent);
			break;
		case DECL_STRUCT:
			DUMPF("(struct %s", decl->name);
			DUMPDECLS(decl->strukt.members);
			DUMPEND();
		case DECL_UNION:
			DUMPF("(union %s", decl->name);
			DUMPDECLS(decl->strukt.members);
			DUMPEND();
		case DECL_ENUM:
			fprintf_indented(file, indent, "(enum %s\n", decl->name);
			fprint_type_info_recursive(file, decl->enums.type_info, indent + 1);
			fprint_decl_list(file, decl->enums.values, indent + 1);
			fprint_endparen(file, indent);
			break;
		case DECL_ERROR:
			fprintf_indented(file, indent, "(error %s\n", decl->name);
			fprint_decl_list(file, decl->error.error_constants, indent + 1);
			fprint_endparen(file, indent);
			break;
		case DECL_ENUM_CONSTANT:
			if (!decl->enum_constant.expr)
			{
				fprintf_indented(file, indent, "(enum-constant %s)\n", decl->name);
				return;
			}
			fprintf_indented(file, indent, "(enum-constant %s\n", decl->name);
			fprint_expr_recursive(file, decl->enum_constant.expr, indent + 1);
			fprint_endparen(file, indent);
			break;
		case DECL_ERROR_CONSTANT:
			fprintf_indented(file, indent, "(error-constant %s)\n", decl->name);
			break;
		case DECL_GENERIC:
			fprintf_indented(file, indent, "(generic %s\n", decl->name);
			fprint_indent(file, indent + 1);
			fprintf(file, "(params\n");
			{
				VECEACH(decl->generic_decl.parameters, i)
				{
					fprint_indent(file, indent + 2);
					fprintf(file, "%s\n", decl->generic_decl.parameters[i].string);
				}
			}
			fprint_endparen(file, indent + 1);
			fprint_indent(file, indent + 1);
			fprintf(file, "(cases\n");
			{
				VECEACH(decl->generic_decl.cases, i)
				{
					fprint_ast_recursive(file, decl->generic_decl.cases[i], indent + 2);
				}
			}
			fprint_endparen(file, indent + 2);
			fprint_endparen(file, indent);
			break;
		case DECL_TYPEDEF:
			fprintf_indented(file, indent, "(typedef %s\n", decl->name);
			if (decl->typedef_decl.is_func)
			{
				fprint_func_signature(file, &decl->typedef_decl.function_signature, indent + 1);
			}
			else
			{
				fprint_type_info_recursive(file, decl->typedef_decl.type_info, indent + 1);
			}
			fprint_endparen(file, indent);
			break;
		case DECL_CT_IF:
			fprintf_indented(file, indent, "(ct-if\n");
			fprint_expr_recursive(file, decl->ct_if_decl.expr, indent + 1);
			fprint_decl_list(file, decl->ct_if_decl.then, indent + 1);
			if (decl->ct_if_decl.elif)
			{
				fprint_decl_recursive(file, decl->ct_if_decl.elif, indent + 1);
			}
			fprint_endparen(file, indent);
			break;
		case DECL_CT_ELIF:
			fprintf_indented(file, indent, "(ct-elif\n");
			fprint_expr_recursive(file, decl->ct_elif_decl.expr, indent + 1);
			fprint_decl_list(file, decl->ct_elif_decl.then, indent + 1);
			if (decl->ct_elif_decl.elif)
			{
				fprint_decl_recursive(file, decl->ct_elif_decl.elif, indent + 1);
			}
			fprint_endparen(file, indent);
			break;
		case DECL_CT_ELSE:
			fprintf_indented(file, indent, "(ct-else\n");
			fprint_decl_list(file, decl->ct_else_decl, indent + 1);
			fprint_endparen(file, indent);
			break;
		case DECL_POISONED:
			fprintf_indented(file, indent, "(poisoned-decl)\n");
			return;
		case DECL_ARRAY_VALUE:
			fprintf_indented(file, indent, "(array value");
			fprint_expr_recursive(file, decl->incr_array_decl, indent + 1);
			fprint_endparen(file, indent);
			return;
		case DECL_IMPORT:
			fprintf_indented(file, indent, "(import %s", decl->name);

			fprint_endparen(file, indent);
			break;
		case DECL_ATTRIBUTE:
			fprintf_indented(file, indent, "(attribute %s", decl->name);
			if (decl->attr.domains & ATTR_FUNC)
			{
				fprintf_indented(file, indent + 1, "(func)");
			}
			if (decl->attr.domains & ATTR_VAR)
			{
				fprintf_indented(file, indent + 1, "(var)");
			}
			if (decl->attr.domains & ATTR_ENUM)
			{
				fprintf_indented(file, indent + 1, "(enum)");
			}
			if (decl->attr.domains & ATTR_STRUCT)
			{
				fprintf_indented(file, indent + 1, "(struct)");
			}
			if (decl->attr.domains & ATTR_UNION)
			{
				fprintf_indented(file, indent + 1, "(union)");
			}
			if (decl->attr.domains & ATTR_CONST)
			{
				fprintf_indented(file, indent + 1, "(const)");
			}
			if (decl->attr.domains & ATTR_ERROR)
			{
				fprintf_indented(file, indent + 1, "(error)");
			}
			if (decl->attr.domains & ATTR_TYPEDEF)
			{
				fprintf_indented(file, indent + 1, "(typedef");
			}
			// TODO attribute
			fprint_endparen(file, indent);
			break;
		case DECL_THROWS:
			fprintf_indented(file, indent, "(throws");
			fprint_type_info_recursive(file, decl->throws, indent + 1);
			fprint_endparen(file, indent);
			break;;
	}
}

static void fprint_decl_list(FILE *file, Decl **decls, int indent)
{
	VECEACH(decls, i)
	{
		fprint_decl_recursive(file, decls[i], indent);
	}
}

static void fprint_asts_recursive(FILE *file, Ast **asts, int indent)
{
	VECEACH(asts, i)
	{
		fprint_ast_recursive(file, asts[i], indent);
	}
}

static void fprint_ast_recursive(FILE *file, Ast *ast, int indent)
{
	if (!ast) return;
	switch (ast->ast_kind)
	{
		case AST_COMPOUND_STMT:
			if (!ast->compound_stmt.stmts)
			{
				DUMP("(compound)");
				return;
			}
			DUMP("(compound\n");
			fprint_asts_recursive(file, ast->compound_stmt.stmts, indent + 1);
			DUMPEND();
		case AST_DECL_EXPR_LIST:
			DUMP("(declexprlist");
			fprint_asts_recursive(file, ast->decl_expr_stmt, indent + 1);
			DUMPEND();
		case AST_DECLARE_STMT:
			DUMP("(declare");
			fprint_decl_recursive(file, ast->declare_stmt, indent + 1);
			DUMPEND();
		case AST_EXPR_STMT:
			DUMP("expr");
			DUMPEXPR(ast->expr_stmt);
			DUMPEND();
			return;
		case AST_WHILE_STMT:
			DUMP("(while");
			DUMPAST(ast->while_stmt.cond);
			DUMPAST(ast->while_stmt.body);
			DUMPEND();
		case AST_SCOPED_STMT:
			DUMP("(scoped");
			DUMPAST(ast->scoped_stmt.stmt);
			DUMPEND();
		case AST_CT_FOR_STMT:
			if (ast->ct_for_stmt.index.string)
			{
				DUMPF("($for %s, %s\n", ast->ct_for_stmt.index.string, ast->ct_for_stmt.value.string);
			}
			else
			{
				DUMPF("($for  %s\n", ast->ct_for_stmt.value.string);
			}
			DUMPEXPR(ast->ct_for_stmt.expr);
			DUMPAST(ast->ct_for_stmt.body);
			DUMPEND();
		case AST_DO_STMT:
			DUMP("(do");
			DUMPAST(ast->do_stmt.body);
			DUMPEXPR(ast->do_stmt.expr);
			DUMPEND();
		case AST_RETURN_STMT:
			if (ast->return_stmt.expr)
			{
				DUMP("(return");
				DUMPEXPR(ast->expr_stmt);
				DUMPEND();
			}
			DUMP("(return)");
			return;
		case AST_BREAK_STMT:
			DUMP("(break)");
			return;
		case AST_NEXT_STMT:
			DUMP("(next)");
			return;
		case AST_CONTINUE_STMT:
			DUMP("(continue)");
			return;
		case AST_DEFAULT_STMT:
			DUMP("(default)");
			return;
		case AST_FOR_STMT:
			DUMP("(for");
			DUMPAST(ast->for_stmt.init);
			DUMPEXPR(ast->for_stmt.cond);
			if (ast->for_stmt.incr)
			{
				DUMPEXPR(ast->for_stmt.incr);
			}
			else
			{
				DUMPI("(noincr)");
			}
			DUMPAST(ast->for_stmt.body);
			DUMPEND();
		case AST_IF_STMT:
			DUMP("(if");
			DUMPAST(ast->if_stmt.cond);
			DUMPAST(ast->if_stmt.then_body);
			DUMPAST(ast->if_stmt.else_body);
			DUMPEND();
	    case AST_SWITCH_STMT:
	    	DUMP("(switch");
            fprint_ast_recursive(file, ast->switch_stmt.cond, indent + 1);
            fprint_asts_recursive(file, ast->switch_stmt.cases, indent + 1);
            DUMPEND();
		case AST_CASE_STMT:
			DUMP("(case");
			DUMPEXPR(ast->case_stmt.expr);
			DUMPEND();
	    case AST_DEFER_STMT:
	    	DUMP("(defer");
	    	DUMPAST(ast->defer_stmt.body);
	    	DUMPEND();
		case AST_GENERIC_CASE_STMT:
			fprintf(file, "(generic-case\n");
			fprint_indent(file, indent + 1);
			fprintf(file, "(match\n");
			{
				VECEACH(ast->generic_case_stmt.types, i)
				{
					fprint_type_info_recursive(file, ast->generic_case_stmt.types[i], indent + 2);
				}
			}
			fprint_endparen(file, indent + 1);
			fprint_ast_recursive(file, ast->generic_case_stmt.body, indent + 1);
			break;
		case AST_GENERIC_DEFAULT_STMT:
			DUMP("(generic-default");
			DUMPAST(ast->generic_default_stmt);
			DUMPEND();
		case AST_POISONED:
			DUMP("(ast-poisoned)");
			return;
		case AST_ASM_STMT:
			TODO
			break;
		case AST_ATTRIBUTE:
			TODO
			break;
		case AST_CATCH_STMT:
			DUMP("(catch");
			fprint_decl_recursive(file, ast->catch_stmt.error_param, indent + 1);
			DUMPAST(ast->catch_stmt.body);
			DUMPEND();
		case AST_CT_IF_STMT:
			TODO
			break;
		case AST_CT_ELIF_STMT:
			TODO
			break;
		case AST_CT_ELSE_STMT:
			TODO
			break;
		case AST_CT_SWITCH_STMT:
			TODO
			break;
		case AST_CT_CASE_STMT:
			TODO
			break;
		case AST_CT_DEFAULT_STMT:
			TODO
			break;
		case AST_GOTO_STMT:
			DUMPF("(goto %s)", ast->goto_stmt.label_name);
			return;
		case AST_LABEL:
			DUMPF("(label %s)", ast->label_stmt.name);
			return;
		case AST_NOP_STMT:
			DUMP("(nop)");
			return;
		case AST_THROW_STMT:
			DUMP("(throw");
			DUMPEXPR(ast->throw_stmt.throw_value);
			DUMPEND();
		case AST_VOLATILE_STMT:
			DUMP("(volatile");
			DUMPAST(ast->volatile_stmt);
			DUMPEND();
	}
	UNREACHABLE
}
void fprint_ast(FILE *file, Ast *ast)
{
	fprint_ast_recursive(file, ast, 0);
}
void fprint_decl(FILE *file, Decl *dec)
{
	fprint_decl_recursive(file, dec, 0);
}
Module poisoned_module = { .name = NULL };
Decl all_error = { .decl_kind = DECL_ERROR, .name = NULL };

