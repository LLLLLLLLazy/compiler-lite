// Generated from /home/code/compiler-lite/frontend/antlr4/MiniC.g4 by ANTLR 4.9.2
import org.antlr.v4.runtime.atn.*;
import org.antlr.v4.runtime.dfa.DFA;
import org.antlr.v4.runtime.*;
import org.antlr.v4.runtime.misc.*;
import org.antlr.v4.runtime.tree.*;
import java.util.List;
import java.util.Iterator;
import java.util.ArrayList;

@SuppressWarnings({"all", "warnings", "unchecked", "unused", "cast"})
public class MiniCParser extends Parser {
	static { RuntimeMetaData.checkVersion("4.9.2", RuntimeMetaData.VERSION); }

	protected static final DFA[] _decisionToDFA;
	protected static final PredictionContextCache _sharedContextCache =
		new PredictionContextCache();
	public static final int
		T_L_PAREN=1, T_R_PAREN=2, T_SEMICOLON=3, T_L_BRACE=4, T_R_BRACE=5, T_L_BRACKET=6, 
		T_R_BRACKET=7, T_ASSIGN=8, T_COMMA=9, T_ADD=10, T_SUB=11, T_MUL=12, T_DIV=13, 
		T_MOD=14, T_RETURN=15, T_INT=16, T_VOID=17, T_ID=18, T_DIGIT=19, LINE_COMMENT=20, 
		BLOCK_COMMENT=21, WS=22;
	public static final int
		RULE_compileUnit = 0, RULE_funcDef = 1, RULE_block = 2, RULE_blockItemList = 3, 
		RULE_blockItem = 4, RULE_varDecl = 5, RULE_basicType = 6, RULE_arrayDimensions = 7, 
		RULE_initList = 8, RULE_initItem = 9, RULE_varDef = 10, RULE_statement = 11, 
		RULE_expr = 12, RULE_addExp = 13, RULE_addOp = 14, RULE_mulExp = 15, RULE_mulOp = 16, 
		RULE_unaryExp = 17, RULE_unaryOp = 18, RULE_primaryExp = 19, RULE_realParamList = 20, 
		RULE_lVal = 21;
	private static String[] makeRuleNames() {
		return new String[] {
			"compileUnit", "funcDef", "block", "blockItemList", "blockItem", "varDecl", 
			"basicType", "arrayDimensions", "initList", "initItem", "varDef", "statement", 
			"expr", "addExp", "addOp", "mulExp", "mulOp", "unaryExp", "unaryOp", 
			"primaryExp", "realParamList", "lVal"
		};
	}
	public static final String[] ruleNames = makeRuleNames();

	private static String[] makeLiteralNames() {
		return new String[] {
			null, "'('", "')'", "';'", "'{'", "'}'", "'['", "']'", "'='", "','", 
			"'+'", "'-'", "'*'", "'/'", "'%'", "'return'", "'int'", "'void'"
		};
	}
	private static final String[] _LITERAL_NAMES = makeLiteralNames();
	private static String[] makeSymbolicNames() {
		return new String[] {
			null, "T_L_PAREN", "T_R_PAREN", "T_SEMICOLON", "T_L_BRACE", "T_R_BRACE", 
			"T_L_BRACKET", "T_R_BRACKET", "T_ASSIGN", "T_COMMA", "T_ADD", "T_SUB", 
			"T_MUL", "T_DIV", "T_MOD", "T_RETURN", "T_INT", "T_VOID", "T_ID", "T_DIGIT", 
			"LINE_COMMENT", "BLOCK_COMMENT", "WS"
		};
	}
	private static final String[] _SYMBOLIC_NAMES = makeSymbolicNames();
	public static final Vocabulary VOCABULARY = new VocabularyImpl(_LITERAL_NAMES, _SYMBOLIC_NAMES);

	/**
	 * @deprecated Use {@link #VOCABULARY} instead.
	 */
	@Deprecated
	public static final String[] tokenNames;
	static {
		tokenNames = new String[_SYMBOLIC_NAMES.length];
		for (int i = 0; i < tokenNames.length; i++) {
			tokenNames[i] = VOCABULARY.getLiteralName(i);
			if (tokenNames[i] == null) {
				tokenNames[i] = VOCABULARY.getSymbolicName(i);
			}

			if (tokenNames[i] == null) {
				tokenNames[i] = "<INVALID>";
			}
		}
	}

	@Override
	@Deprecated
	public String[] getTokenNames() {
		return tokenNames;
	}

	@Override

	public Vocabulary getVocabulary() {
		return VOCABULARY;
	}

	@Override
	public String getGrammarFileName() { return "MiniC.g4"; }

	@Override
	public String[] getRuleNames() { return ruleNames; }

	@Override
	public String getSerializedATN() { return _serializedATN; }

	@Override
	public ATN getATN() { return _ATN; }

	public MiniCParser(TokenStream input) {
		super(input);
		_interp = new ParserATNSimulator(this,_ATN,_decisionToDFA,_sharedContextCache);
	}

	public static class CompileUnitContext extends ParserRuleContext {
		public TerminalNode EOF() { return getToken(MiniCParser.EOF, 0); }
		public List<FuncDefContext> funcDef() {
			return getRuleContexts(FuncDefContext.class);
		}
		public FuncDefContext funcDef(int i) {
			return getRuleContext(FuncDefContext.class,i);
		}
		public List<VarDeclContext> varDecl() {
			return getRuleContexts(VarDeclContext.class);
		}
		public VarDeclContext varDecl(int i) {
			return getRuleContext(VarDeclContext.class,i);
		}
		public CompileUnitContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_compileUnit; }
	}

	public final CompileUnitContext compileUnit() throws RecognitionException {
		CompileUnitContext _localctx = new CompileUnitContext(_ctx, getState());
		enterRule(_localctx, 0, RULE_compileUnit);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(48);
			_errHandler.sync(this);
			_la = _input.LA(1);
			while (_la==T_INT) {
				{
				setState(46);
				_errHandler.sync(this);
				switch ( getInterpreter().adaptivePredict(_input,0,_ctx) ) {
				case 1:
					{
					setState(44);
					funcDef();
					}
					break;
				case 2:
					{
					setState(45);
					varDecl();
					}
					break;
				}
				}
				setState(50);
				_errHandler.sync(this);
				_la = _input.LA(1);
			}
			setState(51);
			match(EOF);
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class FuncDefContext extends ParserRuleContext {
		public TerminalNode T_INT() { return getToken(MiniCParser.T_INT, 0); }
		public TerminalNode T_ID() { return getToken(MiniCParser.T_ID, 0); }
		public TerminalNode T_L_PAREN() { return getToken(MiniCParser.T_L_PAREN, 0); }
		public TerminalNode T_R_PAREN() { return getToken(MiniCParser.T_R_PAREN, 0); }
		public BlockContext block() {
			return getRuleContext(BlockContext.class,0);
		}
		public FuncDefContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_funcDef; }
	}

	public final FuncDefContext funcDef() throws RecognitionException {
		FuncDefContext _localctx = new FuncDefContext(_ctx, getState());
		enterRule(_localctx, 2, RULE_funcDef);
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(53);
			match(T_INT);
			setState(54);
			match(T_ID);
			setState(55);
			match(T_L_PAREN);
			setState(56);
			match(T_R_PAREN);
			setState(57);
			block();
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class BlockContext extends ParserRuleContext {
		public TerminalNode T_L_BRACE() { return getToken(MiniCParser.T_L_BRACE, 0); }
		public TerminalNode T_R_BRACE() { return getToken(MiniCParser.T_R_BRACE, 0); }
		public BlockItemListContext blockItemList() {
			return getRuleContext(BlockItemListContext.class,0);
		}
		public BlockContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_block; }
	}

	public final BlockContext block() throws RecognitionException {
		BlockContext _localctx = new BlockContext(_ctx, getState());
		enterRule(_localctx, 4, RULE_block);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(59);
			match(T_L_BRACE);
			setState(61);
			_errHandler.sync(this);
			_la = _input.LA(1);
			if ((((_la) & ~0x3f) == 0 && ((1L << _la) & ((1L << T_L_PAREN) | (1L << T_SEMICOLON) | (1L << T_L_BRACE) | (1L << T_SUB) | (1L << T_RETURN) | (1L << T_INT) | (1L << T_ID) | (1L << T_DIGIT))) != 0)) {
				{
				setState(60);
				blockItemList();
				}
			}

			setState(63);
			match(T_R_BRACE);
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class BlockItemListContext extends ParserRuleContext {
		public List<BlockItemContext> blockItem() {
			return getRuleContexts(BlockItemContext.class);
		}
		public BlockItemContext blockItem(int i) {
			return getRuleContext(BlockItemContext.class,i);
		}
		public BlockItemListContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_blockItemList; }
	}

	public final BlockItemListContext blockItemList() throws RecognitionException {
		BlockItemListContext _localctx = new BlockItemListContext(_ctx, getState());
		enterRule(_localctx, 6, RULE_blockItemList);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(66); 
			_errHandler.sync(this);
			_la = _input.LA(1);
			do {
				{
				{
				setState(65);
				blockItem();
				}
				}
				setState(68); 
				_errHandler.sync(this);
				_la = _input.LA(1);
			} while ( (((_la) & ~0x3f) == 0 && ((1L << _la) & ((1L << T_L_PAREN) | (1L << T_SEMICOLON) | (1L << T_L_BRACE) | (1L << T_SUB) | (1L << T_RETURN) | (1L << T_INT) | (1L << T_ID) | (1L << T_DIGIT))) != 0) );
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class BlockItemContext extends ParserRuleContext {
		public StatementContext statement() {
			return getRuleContext(StatementContext.class,0);
		}
		public VarDeclContext varDecl() {
			return getRuleContext(VarDeclContext.class,0);
		}
		public BlockItemContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_blockItem; }
	}

	public final BlockItemContext blockItem() throws RecognitionException {
		BlockItemContext _localctx = new BlockItemContext(_ctx, getState());
		enterRule(_localctx, 8, RULE_blockItem);
		try {
			setState(72);
			_errHandler.sync(this);
			switch (_input.LA(1)) {
			case T_L_PAREN:
			case T_SEMICOLON:
			case T_L_BRACE:
			case T_SUB:
			case T_RETURN:
			case T_ID:
			case T_DIGIT:
				enterOuterAlt(_localctx, 1);
				{
				setState(70);
				statement();
				}
				break;
			case T_INT:
				enterOuterAlt(_localctx, 2);
				{
				setState(71);
				varDecl();
				}
				break;
			default:
				throw new NoViableAltException(this);
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class VarDeclContext extends ParserRuleContext {
		public BasicTypeContext basicType() {
			return getRuleContext(BasicTypeContext.class,0);
		}
		public List<VarDefContext> varDef() {
			return getRuleContexts(VarDefContext.class);
		}
		public VarDefContext varDef(int i) {
			return getRuleContext(VarDefContext.class,i);
		}
		public TerminalNode T_SEMICOLON() { return getToken(MiniCParser.T_SEMICOLON, 0); }
		public List<TerminalNode> T_COMMA() { return getTokens(MiniCParser.T_COMMA); }
		public TerminalNode T_COMMA(int i) {
			return getToken(MiniCParser.T_COMMA, i);
		}
		public VarDeclContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_varDecl; }
	}

	public final VarDeclContext varDecl() throws RecognitionException {
		VarDeclContext _localctx = new VarDeclContext(_ctx, getState());
		enterRule(_localctx, 10, RULE_varDecl);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(74);
			basicType();
			setState(75);
			varDef();
			setState(80);
			_errHandler.sync(this);
			_la = _input.LA(1);
			while (_la==T_COMMA) {
				{
				{
				setState(76);
				match(T_COMMA);
				setState(77);
				varDef();
				}
				}
				setState(82);
				_errHandler.sync(this);
				_la = _input.LA(1);
			}
			setState(83);
			match(T_SEMICOLON);
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class BasicTypeContext extends ParserRuleContext {
		public TerminalNode T_INT() { return getToken(MiniCParser.T_INT, 0); }
		public BasicTypeContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_basicType; }
	}

	public final BasicTypeContext basicType() throws RecognitionException {
		BasicTypeContext _localctx = new BasicTypeContext(_ctx, getState());
		enterRule(_localctx, 12, RULE_basicType);
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(85);
			match(T_INT);
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class ArrayDimensionsContext extends ParserRuleContext {
		public List<TerminalNode> T_L_BRACKET() { return getTokens(MiniCParser.T_L_BRACKET); }
		public TerminalNode T_L_BRACKET(int i) {
			return getToken(MiniCParser.T_L_BRACKET, i);
		}
		public List<ExprContext> expr() {
			return getRuleContexts(ExprContext.class);
		}
		public ExprContext expr(int i) {
			return getRuleContext(ExprContext.class,i);
		}
		public List<TerminalNode> T_R_BRACKET() { return getTokens(MiniCParser.T_R_BRACKET); }
		public TerminalNode T_R_BRACKET(int i) {
			return getToken(MiniCParser.T_R_BRACKET, i);
		}
		public ArrayDimensionsContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_arrayDimensions; }
	}

	public final ArrayDimensionsContext arrayDimensions() throws RecognitionException {
		ArrayDimensionsContext _localctx = new ArrayDimensionsContext(_ctx, getState());
		enterRule(_localctx, 14, RULE_arrayDimensions);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(91); 
			_errHandler.sync(this);
			_la = _input.LA(1);
			do {
				{
				{
				setState(87);
				match(T_L_BRACKET);
				setState(88);
				expr();
				setState(89);
				match(T_R_BRACKET);
				}
				}
				setState(93); 
				_errHandler.sync(this);
				_la = _input.LA(1);
			} while ( _la==T_L_BRACKET );
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class InitListContext extends ParserRuleContext {
		public TerminalNode T_L_BRACE() { return getToken(MiniCParser.T_L_BRACE, 0); }
		public TerminalNode T_R_BRACE() { return getToken(MiniCParser.T_R_BRACE, 0); }
		public List<InitItemContext> initItem() {
			return getRuleContexts(InitItemContext.class);
		}
		public InitItemContext initItem(int i) {
			return getRuleContext(InitItemContext.class,i);
		}
		public List<TerminalNode> T_COMMA() { return getTokens(MiniCParser.T_COMMA); }
		public TerminalNode T_COMMA(int i) {
			return getToken(MiniCParser.T_COMMA, i);
		}
		public InitListContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_initList; }
	}

	public final InitListContext initList() throws RecognitionException {
		InitListContext _localctx = new InitListContext(_ctx, getState());
		enterRule(_localctx, 16, RULE_initList);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(95);
			match(T_L_BRACE);
			setState(104);
			_errHandler.sync(this);
			_la = _input.LA(1);
			if ((((_la) & ~0x3f) == 0 && ((1L << _la) & ((1L << T_L_PAREN) | (1L << T_L_BRACE) | (1L << T_SUB) | (1L << T_ID) | (1L << T_DIGIT))) != 0)) {
				{
				setState(96);
				initItem();
				setState(101);
				_errHandler.sync(this);
				_la = _input.LA(1);
				while (_la==T_COMMA) {
					{
					{
					setState(97);
					match(T_COMMA);
					setState(98);
					initItem();
					}
					}
					setState(103);
					_errHandler.sync(this);
					_la = _input.LA(1);
				}
				}
			}

			setState(106);
			match(T_R_BRACE);
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class InitItemContext extends ParserRuleContext {
		public InitListContext initList() {
			return getRuleContext(InitListContext.class,0);
		}
		public ExprContext expr() {
			return getRuleContext(ExprContext.class,0);
		}
		public InitItemContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_initItem; }
	}

	public final InitItemContext initItem() throws RecognitionException {
		InitItemContext _localctx = new InitItemContext(_ctx, getState());
		enterRule(_localctx, 18, RULE_initItem);
		try {
			setState(110);
			_errHandler.sync(this);
			switch (_input.LA(1)) {
			case T_L_BRACE:
				enterOuterAlt(_localctx, 1);
				{
				setState(108);
				initList();
				}
				break;
			case T_L_PAREN:
			case T_SUB:
			case T_ID:
			case T_DIGIT:
				enterOuterAlt(_localctx, 2);
				{
				setState(109);
				expr();
				}
				break;
			default:
				throw new NoViableAltException(this);
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class VarDefContext extends ParserRuleContext {
		public TerminalNode T_ID() { return getToken(MiniCParser.T_ID, 0); }
		public ArrayDimensionsContext arrayDimensions() {
			return getRuleContext(ArrayDimensionsContext.class,0);
		}
		public TerminalNode T_ASSIGN() { return getToken(MiniCParser.T_ASSIGN, 0); }
		public InitListContext initList() {
			return getRuleContext(InitListContext.class,0);
		}
		public ExprContext expr() {
			return getRuleContext(ExprContext.class,0);
		}
		public VarDefContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_varDef; }
	}

	public final VarDefContext varDef() throws RecognitionException {
		VarDefContext _localctx = new VarDefContext(_ctx, getState());
		enterRule(_localctx, 20, RULE_varDef);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(112);
			match(T_ID);
			setState(114);
			_errHandler.sync(this);
			_la = _input.LA(1);
			if (_la==T_L_BRACKET) {
				{
				setState(113);
				arrayDimensions();
				}
			}

			setState(121);
			_errHandler.sync(this);
			_la = _input.LA(1);
			if (_la==T_ASSIGN) {
				{
				setState(116);
				match(T_ASSIGN);
				setState(119);
				_errHandler.sync(this);
				switch (_input.LA(1)) {
				case T_L_BRACE:
					{
					setState(117);
					initList();
					}
					break;
				case T_L_PAREN:
				case T_SUB:
				case T_ID:
				case T_DIGIT:
					{
					setState(118);
					expr();
					}
					break;
				default:
					throw new NoViableAltException(this);
				}
				}
			}

			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class StatementContext extends ParserRuleContext {
		public StatementContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_statement; }
	 
		public StatementContext() { }
		public void copyFrom(StatementContext ctx) {
			super.copyFrom(ctx);
		}
	}
	public static class BlockStatementContext extends StatementContext {
		public BlockContext block() {
			return getRuleContext(BlockContext.class,0);
		}
		public BlockStatementContext(StatementContext ctx) { copyFrom(ctx); }
	}
	public static class AssignStatementContext extends StatementContext {
		public LValContext lVal() {
			return getRuleContext(LValContext.class,0);
		}
		public TerminalNode T_ASSIGN() { return getToken(MiniCParser.T_ASSIGN, 0); }
		public ExprContext expr() {
			return getRuleContext(ExprContext.class,0);
		}
		public TerminalNode T_SEMICOLON() { return getToken(MiniCParser.T_SEMICOLON, 0); }
		public AssignStatementContext(StatementContext ctx) { copyFrom(ctx); }
	}
	public static class ExpressionStatementContext extends StatementContext {
		public TerminalNode T_SEMICOLON() { return getToken(MiniCParser.T_SEMICOLON, 0); }
		public ExprContext expr() {
			return getRuleContext(ExprContext.class,0);
		}
		public ExpressionStatementContext(StatementContext ctx) { copyFrom(ctx); }
	}
	public static class ReturnStatementContext extends StatementContext {
		public TerminalNode T_RETURN() { return getToken(MiniCParser.T_RETURN, 0); }
		public ExprContext expr() {
			return getRuleContext(ExprContext.class,0);
		}
		public TerminalNode T_SEMICOLON() { return getToken(MiniCParser.T_SEMICOLON, 0); }
		public ReturnStatementContext(StatementContext ctx) { copyFrom(ctx); }
	}

	public final StatementContext statement() throws RecognitionException {
		StatementContext _localctx = new StatementContext(_ctx, getState());
		enterRule(_localctx, 22, RULE_statement);
		int _la;
		try {
			setState(137);
			_errHandler.sync(this);
			switch ( getInterpreter().adaptivePredict(_input,14,_ctx) ) {
			case 1:
				_localctx = new ReturnStatementContext(_localctx);
				enterOuterAlt(_localctx, 1);
				{
				setState(123);
				match(T_RETURN);
				setState(124);
				expr();
				setState(125);
				match(T_SEMICOLON);
				}
				break;
			case 2:
				_localctx = new AssignStatementContext(_localctx);
				enterOuterAlt(_localctx, 2);
				{
				setState(127);
				lVal();
				setState(128);
				match(T_ASSIGN);
				setState(129);
				expr();
				setState(130);
				match(T_SEMICOLON);
				}
				break;
			case 3:
				_localctx = new BlockStatementContext(_localctx);
				enterOuterAlt(_localctx, 3);
				{
				setState(132);
				block();
				}
				break;
			case 4:
				_localctx = new ExpressionStatementContext(_localctx);
				enterOuterAlt(_localctx, 4);
				{
				setState(134);
				_errHandler.sync(this);
				_la = _input.LA(1);
				if ((((_la) & ~0x3f) == 0 && ((1L << _la) & ((1L << T_L_PAREN) | (1L << T_SUB) | (1L << T_ID) | (1L << T_DIGIT))) != 0)) {
					{
					setState(133);
					expr();
					}
				}

				setState(136);
				match(T_SEMICOLON);
				}
				break;
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class ExprContext extends ParserRuleContext {
		public AddExpContext addExp() {
			return getRuleContext(AddExpContext.class,0);
		}
		public ExprContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_expr; }
	}

	public final ExprContext expr() throws RecognitionException {
		ExprContext _localctx = new ExprContext(_ctx, getState());
		enterRule(_localctx, 24, RULE_expr);
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(139);
			addExp();
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class AddExpContext extends ParserRuleContext {
		public List<MulExpContext> mulExp() {
			return getRuleContexts(MulExpContext.class);
		}
		public MulExpContext mulExp(int i) {
			return getRuleContext(MulExpContext.class,i);
		}
		public List<AddOpContext> addOp() {
			return getRuleContexts(AddOpContext.class);
		}
		public AddOpContext addOp(int i) {
			return getRuleContext(AddOpContext.class,i);
		}
		public AddExpContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_addExp; }
	}

	public final AddExpContext addExp() throws RecognitionException {
		AddExpContext _localctx = new AddExpContext(_ctx, getState());
		enterRule(_localctx, 26, RULE_addExp);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(141);
			mulExp();
			setState(147);
			_errHandler.sync(this);
			_la = _input.LA(1);
			while (_la==T_ADD || _la==T_SUB) {
				{
				{
				setState(142);
				addOp();
				setState(143);
				mulExp();
				}
				}
				setState(149);
				_errHandler.sync(this);
				_la = _input.LA(1);
			}
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class AddOpContext extends ParserRuleContext {
		public TerminalNode T_ADD() { return getToken(MiniCParser.T_ADD, 0); }
		public TerminalNode T_SUB() { return getToken(MiniCParser.T_SUB, 0); }
		public AddOpContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_addOp; }
	}

	public final AddOpContext addOp() throws RecognitionException {
		AddOpContext _localctx = new AddOpContext(_ctx, getState());
		enterRule(_localctx, 28, RULE_addOp);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(150);
			_la = _input.LA(1);
			if ( !(_la==T_ADD || _la==T_SUB) ) {
			_errHandler.recoverInline(this);
			}
			else {
				if ( _input.LA(1)==Token.EOF ) matchedEOF = true;
				_errHandler.reportMatch(this);
				consume();
			}
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class MulExpContext extends ParserRuleContext {
		public List<UnaryExpContext> unaryExp() {
			return getRuleContexts(UnaryExpContext.class);
		}
		public UnaryExpContext unaryExp(int i) {
			return getRuleContext(UnaryExpContext.class,i);
		}
		public List<MulOpContext> mulOp() {
			return getRuleContexts(MulOpContext.class);
		}
		public MulOpContext mulOp(int i) {
			return getRuleContext(MulOpContext.class,i);
		}
		public MulExpContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_mulExp; }
	}

	public final MulExpContext mulExp() throws RecognitionException {
		MulExpContext _localctx = new MulExpContext(_ctx, getState());
		enterRule(_localctx, 30, RULE_mulExp);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(152);
			unaryExp();
			setState(158);
			_errHandler.sync(this);
			_la = _input.LA(1);
			while ((((_la) & ~0x3f) == 0 && ((1L << _la) & ((1L << T_MUL) | (1L << T_DIV) | (1L << T_MOD))) != 0)) {
				{
				{
				setState(153);
				mulOp();
				setState(154);
				unaryExp();
				}
				}
				setState(160);
				_errHandler.sync(this);
				_la = _input.LA(1);
			}
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class MulOpContext extends ParserRuleContext {
		public TerminalNode T_MUL() { return getToken(MiniCParser.T_MUL, 0); }
		public TerminalNode T_DIV() { return getToken(MiniCParser.T_DIV, 0); }
		public TerminalNode T_MOD() { return getToken(MiniCParser.T_MOD, 0); }
		public MulOpContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_mulOp; }
	}

	public final MulOpContext mulOp() throws RecognitionException {
		MulOpContext _localctx = new MulOpContext(_ctx, getState());
		enterRule(_localctx, 32, RULE_mulOp);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(161);
			_la = _input.LA(1);
			if ( !((((_la) & ~0x3f) == 0 && ((1L << _la) & ((1L << T_MUL) | (1L << T_DIV) | (1L << T_MOD))) != 0)) ) {
			_errHandler.recoverInline(this);
			}
			else {
				if ( _input.LA(1)==Token.EOF ) matchedEOF = true;
				_errHandler.reportMatch(this);
				consume();
			}
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class UnaryExpContext extends ParserRuleContext {
		public PrimaryExpContext primaryExp() {
			return getRuleContext(PrimaryExpContext.class,0);
		}
		public TerminalNode T_ID() { return getToken(MiniCParser.T_ID, 0); }
		public TerminalNode T_L_PAREN() { return getToken(MiniCParser.T_L_PAREN, 0); }
		public TerminalNode T_R_PAREN() { return getToken(MiniCParser.T_R_PAREN, 0); }
		public RealParamListContext realParamList() {
			return getRuleContext(RealParamListContext.class,0);
		}
		public UnaryOpContext unaryOp() {
			return getRuleContext(UnaryOpContext.class,0);
		}
		public UnaryExpContext unaryExp() {
			return getRuleContext(UnaryExpContext.class,0);
		}
		public UnaryExpContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_unaryExp; }
	}

	public final UnaryExpContext unaryExp() throws RecognitionException {
		UnaryExpContext _localctx = new UnaryExpContext(_ctx, getState());
		enterRule(_localctx, 34, RULE_unaryExp);
		int _la;
		try {
			setState(173);
			_errHandler.sync(this);
			switch ( getInterpreter().adaptivePredict(_input,18,_ctx) ) {
			case 1:
				enterOuterAlt(_localctx, 1);
				{
				setState(163);
				primaryExp();
				}
				break;
			case 2:
				enterOuterAlt(_localctx, 2);
				{
				setState(164);
				match(T_ID);
				setState(165);
				match(T_L_PAREN);
				setState(167);
				_errHandler.sync(this);
				_la = _input.LA(1);
				if ((((_la) & ~0x3f) == 0 && ((1L << _la) & ((1L << T_L_PAREN) | (1L << T_SUB) | (1L << T_ID) | (1L << T_DIGIT))) != 0)) {
					{
					setState(166);
					realParamList();
					}
				}

				setState(169);
				match(T_R_PAREN);
				}
				break;
			case 3:
				enterOuterAlt(_localctx, 3);
				{
				setState(170);
				unaryOp();
				setState(171);
				unaryExp();
				}
				break;
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class UnaryOpContext extends ParserRuleContext {
		public TerminalNode T_SUB() { return getToken(MiniCParser.T_SUB, 0); }
		public UnaryOpContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_unaryOp; }
	}

	public final UnaryOpContext unaryOp() throws RecognitionException {
		UnaryOpContext _localctx = new UnaryOpContext(_ctx, getState());
		enterRule(_localctx, 36, RULE_unaryOp);
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(175);
			match(T_SUB);
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class PrimaryExpContext extends ParserRuleContext {
		public TerminalNode T_L_PAREN() { return getToken(MiniCParser.T_L_PAREN, 0); }
		public ExprContext expr() {
			return getRuleContext(ExprContext.class,0);
		}
		public TerminalNode T_R_PAREN() { return getToken(MiniCParser.T_R_PAREN, 0); }
		public TerminalNode T_DIGIT() { return getToken(MiniCParser.T_DIGIT, 0); }
		public LValContext lVal() {
			return getRuleContext(LValContext.class,0);
		}
		public PrimaryExpContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_primaryExp; }
	}

	public final PrimaryExpContext primaryExp() throws RecognitionException {
		PrimaryExpContext _localctx = new PrimaryExpContext(_ctx, getState());
		enterRule(_localctx, 38, RULE_primaryExp);
		try {
			setState(183);
			_errHandler.sync(this);
			switch (_input.LA(1)) {
			case T_L_PAREN:
				enterOuterAlt(_localctx, 1);
				{
				setState(177);
				match(T_L_PAREN);
				setState(178);
				expr();
				setState(179);
				match(T_R_PAREN);
				}
				break;
			case T_DIGIT:
				enterOuterAlt(_localctx, 2);
				{
				setState(181);
				match(T_DIGIT);
				}
				break;
			case T_ID:
				enterOuterAlt(_localctx, 3);
				{
				setState(182);
				lVal();
				}
				break;
			default:
				throw new NoViableAltException(this);
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class RealParamListContext extends ParserRuleContext {
		public List<ExprContext> expr() {
			return getRuleContexts(ExprContext.class);
		}
		public ExprContext expr(int i) {
			return getRuleContext(ExprContext.class,i);
		}
		public List<TerminalNode> T_COMMA() { return getTokens(MiniCParser.T_COMMA); }
		public TerminalNode T_COMMA(int i) {
			return getToken(MiniCParser.T_COMMA, i);
		}
		public RealParamListContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_realParamList; }
	}

	public final RealParamListContext realParamList() throws RecognitionException {
		RealParamListContext _localctx = new RealParamListContext(_ctx, getState());
		enterRule(_localctx, 40, RULE_realParamList);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(185);
			expr();
			setState(190);
			_errHandler.sync(this);
			_la = _input.LA(1);
			while (_la==T_COMMA) {
				{
				{
				setState(186);
				match(T_COMMA);
				setState(187);
				expr();
				}
				}
				setState(192);
				_errHandler.sync(this);
				_la = _input.LA(1);
			}
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static class LValContext extends ParserRuleContext {
		public TerminalNode T_ID() { return getToken(MiniCParser.T_ID, 0); }
		public List<TerminalNode> T_L_BRACKET() { return getTokens(MiniCParser.T_L_BRACKET); }
		public TerminalNode T_L_BRACKET(int i) {
			return getToken(MiniCParser.T_L_BRACKET, i);
		}
		public List<ExprContext> expr() {
			return getRuleContexts(ExprContext.class);
		}
		public ExprContext expr(int i) {
			return getRuleContext(ExprContext.class,i);
		}
		public List<TerminalNode> T_R_BRACKET() { return getTokens(MiniCParser.T_R_BRACKET); }
		public TerminalNode T_R_BRACKET(int i) {
			return getToken(MiniCParser.T_R_BRACKET, i);
		}
		public LValContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_lVal; }
	}

	public final LValContext lVal() throws RecognitionException {
		LValContext _localctx = new LValContext(_ctx, getState());
		enterRule(_localctx, 42, RULE_lVal);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(193);
			match(T_ID);
			setState(200);
			_errHandler.sync(this);
			_la = _input.LA(1);
			while (_la==T_L_BRACKET) {
				{
				{
				setState(194);
				match(T_L_BRACKET);
				setState(195);
				expr();
				setState(196);
				match(T_R_BRACKET);
				}
				}
				setState(202);
				_errHandler.sync(this);
				_la = _input.LA(1);
			}
			}
		}
		catch (RecognitionException re) {
			_localctx.exception = re;
			_errHandler.reportError(this, re);
			_errHandler.recover(this, re);
		}
		finally {
			exitRule();
		}
		return _localctx;
	}

	public static final String _serializedATN =
		"\3\u608b\ua72a\u8133\ub9ed\u417c\u3be7\u7786\u5964\3\30\u00ce\4\2\t\2"+
		"\4\3\t\3\4\4\t\4\4\5\t\5\4\6\t\6\4\7\t\7\4\b\t\b\4\t\t\t\4\n\t\n\4\13"+
		"\t\13\4\f\t\f\4\r\t\r\4\16\t\16\4\17\t\17\4\20\t\20\4\21\t\21\4\22\t\22"+
		"\4\23\t\23\4\24\t\24\4\25\t\25\4\26\t\26\4\27\t\27\3\2\3\2\7\2\61\n\2"+
		"\f\2\16\2\64\13\2\3\2\3\2\3\3\3\3\3\3\3\3\3\3\3\3\3\4\3\4\5\4@\n\4\3\4"+
		"\3\4\3\5\6\5E\n\5\r\5\16\5F\3\6\3\6\5\6K\n\6\3\7\3\7\3\7\3\7\7\7Q\n\7"+
		"\f\7\16\7T\13\7\3\7\3\7\3\b\3\b\3\t\3\t\3\t\3\t\6\t^\n\t\r\t\16\t_\3\n"+
		"\3\n\3\n\3\n\7\nf\n\n\f\n\16\ni\13\n\5\nk\n\n\3\n\3\n\3\13\3\13\5\13q"+
		"\n\13\3\f\3\f\5\fu\n\f\3\f\3\f\3\f\5\fz\n\f\5\f|\n\f\3\r\3\r\3\r\3\r\3"+
		"\r\3\r\3\r\3\r\3\r\3\r\3\r\5\r\u0089\n\r\3\r\5\r\u008c\n\r\3\16\3\16\3"+
		"\17\3\17\3\17\3\17\7\17\u0094\n\17\f\17\16\17\u0097\13\17\3\20\3\20\3"+
		"\21\3\21\3\21\3\21\7\21\u009f\n\21\f\21\16\21\u00a2\13\21\3\22\3\22\3"+
		"\23\3\23\3\23\3\23\5\23\u00aa\n\23\3\23\3\23\3\23\3\23\5\23\u00b0\n\23"+
		"\3\24\3\24\3\25\3\25\3\25\3\25\3\25\3\25\5\25\u00ba\n\25\3\26\3\26\3\26"+
		"\7\26\u00bf\n\26\f\26\16\26\u00c2\13\26\3\27\3\27\3\27\3\27\3\27\7\27"+
		"\u00c9\n\27\f\27\16\27\u00cc\13\27\3\27\2\2\30\2\4\6\b\n\f\16\20\22\24"+
		"\26\30\32\34\36 \"$&(*,\2\4\3\2\f\r\3\2\16\20\2\u00d1\2\62\3\2\2\2\4\67"+
		"\3\2\2\2\6=\3\2\2\2\bD\3\2\2\2\nJ\3\2\2\2\fL\3\2\2\2\16W\3\2\2\2\20]\3"+
		"\2\2\2\22a\3\2\2\2\24p\3\2\2\2\26r\3\2\2\2\30\u008b\3\2\2\2\32\u008d\3"+
		"\2\2\2\34\u008f\3\2\2\2\36\u0098\3\2\2\2 \u009a\3\2\2\2\"\u00a3\3\2\2"+
		"\2$\u00af\3\2\2\2&\u00b1\3\2\2\2(\u00b9\3\2\2\2*\u00bb\3\2\2\2,\u00c3"+
		"\3\2\2\2.\61\5\4\3\2/\61\5\f\7\2\60.\3\2\2\2\60/\3\2\2\2\61\64\3\2\2\2"+
		"\62\60\3\2\2\2\62\63\3\2\2\2\63\65\3\2\2\2\64\62\3\2\2\2\65\66\7\2\2\3"+
		"\66\3\3\2\2\2\678\7\22\2\289\7\24\2\29:\7\3\2\2:;\7\4\2\2;<\5\6\4\2<\5"+
		"\3\2\2\2=?\7\6\2\2>@\5\b\5\2?>\3\2\2\2?@\3\2\2\2@A\3\2\2\2AB\7\7\2\2B"+
		"\7\3\2\2\2CE\5\n\6\2DC\3\2\2\2EF\3\2\2\2FD\3\2\2\2FG\3\2\2\2G\t\3\2\2"+
		"\2HK\5\30\r\2IK\5\f\7\2JH\3\2\2\2JI\3\2\2\2K\13\3\2\2\2LM\5\16\b\2MR\5"+
		"\26\f\2NO\7\13\2\2OQ\5\26\f\2PN\3\2\2\2QT\3\2\2\2RP\3\2\2\2RS\3\2\2\2"+
		"SU\3\2\2\2TR\3\2\2\2UV\7\5\2\2V\r\3\2\2\2WX\7\22\2\2X\17\3\2\2\2YZ\7\b"+
		"\2\2Z[\5\32\16\2[\\\7\t\2\2\\^\3\2\2\2]Y\3\2\2\2^_\3\2\2\2_]\3\2\2\2_"+
		"`\3\2\2\2`\21\3\2\2\2aj\7\6\2\2bg\5\24\13\2cd\7\13\2\2df\5\24\13\2ec\3"+
		"\2\2\2fi\3\2\2\2ge\3\2\2\2gh\3\2\2\2hk\3\2\2\2ig\3\2\2\2jb\3\2\2\2jk\3"+
		"\2\2\2kl\3\2\2\2lm\7\7\2\2m\23\3\2\2\2nq\5\22\n\2oq\5\32\16\2pn\3\2\2"+
		"\2po\3\2\2\2q\25\3\2\2\2rt\7\24\2\2su\5\20\t\2ts\3\2\2\2tu\3\2\2\2u{\3"+
		"\2\2\2vy\7\n\2\2wz\5\22\n\2xz\5\32\16\2yw\3\2\2\2yx\3\2\2\2z|\3\2\2\2"+
		"{v\3\2\2\2{|\3\2\2\2|\27\3\2\2\2}~\7\21\2\2~\177\5\32\16\2\177\u0080\7"+
		"\5\2\2\u0080\u008c\3\2\2\2\u0081\u0082\5,\27\2\u0082\u0083\7\n\2\2\u0083"+
		"\u0084\5\32\16\2\u0084\u0085\7\5\2\2\u0085\u008c\3\2\2\2\u0086\u008c\5"+
		"\6\4\2\u0087\u0089\5\32\16\2\u0088\u0087\3\2\2\2\u0088\u0089\3\2\2\2\u0089"+
		"\u008a\3\2\2\2\u008a\u008c\7\5\2\2\u008b}\3\2\2\2\u008b\u0081\3\2\2\2"+
		"\u008b\u0086\3\2\2\2\u008b\u0088\3\2\2\2\u008c\31\3\2\2\2\u008d\u008e"+
		"\5\34\17\2\u008e\33\3\2\2\2\u008f\u0095\5 \21\2\u0090\u0091\5\36\20\2"+
		"\u0091\u0092\5 \21\2\u0092\u0094\3\2\2\2\u0093\u0090\3\2\2\2\u0094\u0097"+
		"\3\2\2\2\u0095\u0093\3\2\2\2\u0095\u0096\3\2\2\2\u0096\35\3\2\2\2\u0097"+
		"\u0095\3\2\2\2\u0098\u0099\t\2\2\2\u0099\37\3\2\2\2\u009a\u00a0\5$\23"+
		"\2\u009b\u009c\5\"\22\2\u009c\u009d\5$\23\2\u009d\u009f\3\2\2\2\u009e"+
		"\u009b\3\2\2\2\u009f\u00a2\3\2\2\2\u00a0\u009e\3\2\2\2\u00a0\u00a1\3\2"+
		"\2\2\u00a1!\3\2\2\2\u00a2\u00a0\3\2\2\2\u00a3\u00a4\t\3\2\2\u00a4#\3\2"+
		"\2\2\u00a5\u00b0\5(\25\2\u00a6\u00a7\7\24\2\2\u00a7\u00a9\7\3\2\2\u00a8"+
		"\u00aa\5*\26\2\u00a9\u00a8\3\2\2\2\u00a9\u00aa\3\2\2\2\u00aa\u00ab\3\2"+
		"\2\2\u00ab\u00b0\7\4\2\2\u00ac\u00ad\5&\24\2\u00ad\u00ae\5$\23\2\u00ae"+
		"\u00b0\3\2\2\2\u00af\u00a5\3\2\2\2\u00af\u00a6\3\2\2\2\u00af\u00ac\3\2"+
		"\2\2\u00b0%\3\2\2\2\u00b1\u00b2\7\r\2\2\u00b2\'\3\2\2\2\u00b3\u00b4\7"+
		"\3\2\2\u00b4\u00b5\5\32\16\2\u00b5\u00b6\7\4\2\2\u00b6\u00ba\3\2\2\2\u00b7"+
		"\u00ba\7\25\2\2\u00b8\u00ba\5,\27\2\u00b9\u00b3\3\2\2\2\u00b9\u00b7\3"+
		"\2\2\2\u00b9\u00b8\3\2\2\2\u00ba)\3\2\2\2\u00bb\u00c0\5\32\16\2\u00bc"+
		"\u00bd\7\13\2\2\u00bd\u00bf\5\32\16\2\u00be\u00bc\3\2\2\2\u00bf\u00c2"+
		"\3\2\2\2\u00c0\u00be\3\2\2\2\u00c0\u00c1\3\2\2\2\u00c1+\3\2\2\2\u00c2"+
		"\u00c0\3\2\2\2\u00c3\u00ca\7\24\2\2\u00c4\u00c5\7\b\2\2\u00c5\u00c6\5"+
		"\32\16\2\u00c6\u00c7\7\t\2\2\u00c7\u00c9\3\2\2\2\u00c8\u00c4\3\2\2\2\u00c9"+
		"\u00cc\3\2\2\2\u00ca\u00c8\3\2\2\2\u00ca\u00cb\3\2\2\2\u00cb-\3\2\2\2"+
		"\u00cc\u00ca\3\2\2\2\30\60\62?FJR_gjpty{\u0088\u008b\u0095\u00a0\u00a9"+
		"\u00af\u00b9\u00c0\u00ca";
	public static final ATN _ATN =
		new ATNDeserializer().deserialize(_serializedATN.toCharArray());
	static {
		_decisionToDFA = new DFA[_ATN.getNumberOfDecisions()];
		for (int i = 0; i < _ATN.getNumberOfDecisions(); i++) {
			_decisionToDFA[i] = new DFA(_ATN.getDecisionState(i), i);
		}
	}
}