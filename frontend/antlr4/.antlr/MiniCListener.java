// Generated from /home/code/compiler-lite/frontend/antlr4/MiniC.g4 by ANTLR 4.9.2
import org.antlr.v4.runtime.tree.ParseTreeListener;

/**
 * This interface defines a complete listener for a parse tree produced by
 * {@link MiniCParser}.
 */
public interface MiniCListener extends ParseTreeListener {
	/**
	 * Enter a parse tree produced by {@link MiniCParser#compileUnit}.
	 * @param ctx the parse tree
	 */
	void enterCompileUnit(MiniCParser.CompileUnitContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#compileUnit}.
	 * @param ctx the parse tree
	 */
	void exitCompileUnit(MiniCParser.CompileUnitContext ctx);
	/**
	 * Enter a parse tree produced by {@link MiniCParser#funcDef}.
	 * @param ctx the parse tree
	 */
	void enterFuncDef(MiniCParser.FuncDefContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#funcDef}.
	 * @param ctx the parse tree
	 */
	void exitFuncDef(MiniCParser.FuncDefContext ctx);
	/**
	 * Enter a parse tree produced by {@link MiniCParser#block}.
	 * @param ctx the parse tree
	 */
	void enterBlock(MiniCParser.BlockContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#block}.
	 * @param ctx the parse tree
	 */
	void exitBlock(MiniCParser.BlockContext ctx);
	/**
	 * Enter a parse tree produced by {@link MiniCParser#blockItemList}.
	 * @param ctx the parse tree
	 */
	void enterBlockItemList(MiniCParser.BlockItemListContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#blockItemList}.
	 * @param ctx the parse tree
	 */
	void exitBlockItemList(MiniCParser.BlockItemListContext ctx);
	/**
	 * Enter a parse tree produced by {@link MiniCParser#blockItem}.
	 * @param ctx the parse tree
	 */
	void enterBlockItem(MiniCParser.BlockItemContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#blockItem}.
	 * @param ctx the parse tree
	 */
	void exitBlockItem(MiniCParser.BlockItemContext ctx);
	/**
	 * Enter a parse tree produced by {@link MiniCParser#varDecl}.
	 * @param ctx the parse tree
	 */
	void enterVarDecl(MiniCParser.VarDeclContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#varDecl}.
	 * @param ctx the parse tree
	 */
	void exitVarDecl(MiniCParser.VarDeclContext ctx);
	/**
	 * Enter a parse tree produced by {@link MiniCParser#basicType}.
	 * @param ctx the parse tree
	 */
	void enterBasicType(MiniCParser.BasicTypeContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#basicType}.
	 * @param ctx the parse tree
	 */
	void exitBasicType(MiniCParser.BasicTypeContext ctx);
	/**
	 * Enter a parse tree produced by {@link MiniCParser#varDef}.
	 * @param ctx the parse tree
	 */
	void enterVarDef(MiniCParser.VarDefContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#varDef}.
	 * @param ctx the parse tree
	 */
	void exitVarDef(MiniCParser.VarDefContext ctx);
	/**
	 * Enter a parse tree produced by the {@code returnStatement}
	 * labeled alternative in {@link MiniCParser#statement}.
	 * @param ctx the parse tree
	 */
	void enterReturnStatement(MiniCParser.ReturnStatementContext ctx);
	/**
	 * Exit a parse tree produced by the {@code returnStatement}
	 * labeled alternative in {@link MiniCParser#statement}.
	 * @param ctx the parse tree
	 */
	void exitReturnStatement(MiniCParser.ReturnStatementContext ctx);
	/**
	 * Enter a parse tree produced by the {@code assignStatement}
	 * labeled alternative in {@link MiniCParser#statement}.
	 * @param ctx the parse tree
	 */
	void enterAssignStatement(MiniCParser.AssignStatementContext ctx);
	/**
	 * Exit a parse tree produced by the {@code assignStatement}
	 * labeled alternative in {@link MiniCParser#statement}.
	 * @param ctx the parse tree
	 */
	void exitAssignStatement(MiniCParser.AssignStatementContext ctx);
	/**
	 * Enter a parse tree produced by the {@code blockStatement}
	 * labeled alternative in {@link MiniCParser#statement}.
	 * @param ctx the parse tree
	 */
	void enterBlockStatement(MiniCParser.BlockStatementContext ctx);
	/**
	 * Exit a parse tree produced by the {@code blockStatement}
	 * labeled alternative in {@link MiniCParser#statement}.
	 * @param ctx the parse tree
	 */
	void exitBlockStatement(MiniCParser.BlockStatementContext ctx);
	/**
	 * Enter a parse tree produced by the {@code expressionStatement}
	 * labeled alternative in {@link MiniCParser#statement}.
	 * @param ctx the parse tree
	 */
	void enterExpressionStatement(MiniCParser.ExpressionStatementContext ctx);
	/**
	 * Exit a parse tree produced by the {@code expressionStatement}
	 * labeled alternative in {@link MiniCParser#statement}.
	 * @param ctx the parse tree
	 */
	void exitExpressionStatement(MiniCParser.ExpressionStatementContext ctx);
	/**
	 * Enter a parse tree produced by {@link MiniCParser#expr}.
	 * @param ctx the parse tree
	 */
	void enterExpr(MiniCParser.ExprContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#expr}.
	 * @param ctx the parse tree
	 */
	void exitExpr(MiniCParser.ExprContext ctx);
	/**
	 * Enter a parse tree produced by {@link MiniCParser#addExp}.
	 * @param ctx the parse tree
	 */
	void enterAddExp(MiniCParser.AddExpContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#addExp}.
	 * @param ctx the parse tree
	 */
	void exitAddExp(MiniCParser.AddExpContext ctx);
	/**
	 * Enter a parse tree produced by {@link MiniCParser#addOp}.
	 * @param ctx the parse tree
	 */
	void enterAddOp(MiniCParser.AddOpContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#addOp}.
	 * @param ctx the parse tree
	 */
	void exitAddOp(MiniCParser.AddOpContext ctx);
	/**
	 * Enter a parse tree produced by {@link MiniCParser#unaryExp}.
	 * @param ctx the parse tree
	 */
	void enterUnaryExp(MiniCParser.UnaryExpContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#unaryExp}.
	 * @param ctx the parse tree
	 */
	void exitUnaryExp(MiniCParser.UnaryExpContext ctx);
	/**
	 * Enter a parse tree produced by {@link MiniCParser#primaryExp}.
	 * @param ctx the parse tree
	 */
	void enterPrimaryExp(MiniCParser.PrimaryExpContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#primaryExp}.
	 * @param ctx the parse tree
	 */
	void exitPrimaryExp(MiniCParser.PrimaryExpContext ctx);
	/**
	 * Enter a parse tree produced by {@link MiniCParser#realParamList}.
	 * @param ctx the parse tree
	 */
	void enterRealParamList(MiniCParser.RealParamListContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#realParamList}.
	 * @param ctx the parse tree
	 */
	void exitRealParamList(MiniCParser.RealParamListContext ctx);
	/**
	 * Enter a parse tree produced by {@link MiniCParser#lVal}.
	 * @param ctx the parse tree
	 */
	void enterLVal(MiniCParser.LValContext ctx);
	/**
	 * Exit a parse tree produced by {@link MiniCParser#lVal}.
	 * @param ctx the parse tree
	 */
	void exitLVal(MiniCParser.LValContext ctx);
}