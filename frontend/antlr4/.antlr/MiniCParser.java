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
		T_R_BRACKET=7, T_ASSIGN=8, T_COMMA=9, T_ADD=10, T_SUB=11, T_RETURN=12, 
		T_INT=13, T_VOID=14, T_ID=15, T_DIGIT=16, LINE_COMMENT=17, BLOCK_COMMENT=18, 
		WS=19;
	public static final int
		RULE_compileUnit = 0, RULE_funcDef = 1, RULE_block = 2, RULE_blockItemList = 3, 
		RULE_blockItem = 4, RULE_varDecl = 5, RULE_basicType = 6, RULE_arrayDimensions = 7, 
		RULE_initList = 8, RULE_initItem = 9, RULE_varDef = 10, RULE_statement = 11, 
		RULE_expr = 12, RULE_addExp = 13, RULE_addOp = 14, RULE_unaryExp = 15, 
		RULE_primaryExp = 16, RULE_realParamList = 17, RULE_lVal = 18;
	private static String[] makeRuleNames() {
		return new String[] {
			"compileUnit", "funcDef", "block", "blockItemList", "blockItem", "varDecl", 
			"basicType", "arrayDimensions", "initList", "initItem", "varDef", "statement", 
			"expr", "addExp", "addOp", "unaryExp", "primaryExp", "realParamList", 
			"lVal"
		};
	}
	public static final String[] ruleNames = makeRuleNames();

	private static String[] makeLiteralNames() {
		return new String[] {
			null, "'('", "')'", "';'", "'{'", "'}'", "'['", "']'", "'='", "','", 
			"'+'", "'-'", "'return'", "'int'", "'void'"
		};
	}
	private static final String[] _LITERAL_NAMES = makeLiteralNames();
	private static String[] makeSymbolicNames() {
		return new String[] {
			null, "T_L_PAREN", "T_R_PAREN", "T_SEMICOLON", "T_L_BRACE", "T_R_BRACE", 
			"T_L_BRACKET", "T_R_BRACKET", "T_ASSIGN", "T_COMMA", "T_ADD", "T_SUB", 
			"T_RETURN", "T_INT", "T_VOID", "T_ID", "T_DIGIT", "LINE_COMMENT", "BLOCK_COMMENT", 
			"WS"
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
			setState(42);
			_errHandler.sync(this);
			_la = _input.LA(1);
			while (_la==T_INT) {
				{
				setState(40);
				_errHandler.sync(this);
				switch ( getInterpreter().adaptivePredict(_input,0,_ctx) ) {
				case 1:
					{
					setState(38);
					funcDef();
					}
					break;
				case 2:
					{
					setState(39);
					varDecl();
					}
					break;
				}
				}
				setState(44);
				_errHandler.sync(this);
				_la = _input.LA(1);
			}
			setState(45);
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
			setState(47);
			match(T_INT);
			setState(48);
			match(T_ID);
			setState(49);
			match(T_L_PAREN);
			setState(50);
			match(T_R_PAREN);
			setState(51);
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
			setState(53);
			match(T_L_BRACE);
			setState(55);
			_errHandler.sync(this);
			_la = _input.LA(1);
			if ((((_la) & ~0x3f) == 0 && ((1L << _la) & ((1L << T_L_PAREN) | (1L << T_SEMICOLON) | (1L << T_L_BRACE) | (1L << T_RETURN) | (1L << T_INT) | (1L << T_ID) | (1L << T_DIGIT))) != 0)) {
				{
				setState(54);
				blockItemList();
				}
			}

			setState(57);
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
			setState(60); 
			_errHandler.sync(this);
			_la = _input.LA(1);
			do {
				{
				{
				setState(59);
				blockItem();
				}
				}
				setState(62); 
				_errHandler.sync(this);
				_la = _input.LA(1);
			} while ( (((_la) & ~0x3f) == 0 && ((1L << _la) & ((1L << T_L_PAREN) | (1L << T_SEMICOLON) | (1L << T_L_BRACE) | (1L << T_RETURN) | (1L << T_INT) | (1L << T_ID) | (1L << T_DIGIT))) != 0) );
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
			setState(66);
			_errHandler.sync(this);
			switch (_input.LA(1)) {
			case T_L_PAREN:
			case T_SEMICOLON:
			case T_L_BRACE:
			case T_RETURN:
			case T_ID:
			case T_DIGIT:
				enterOuterAlt(_localctx, 1);
				{
				setState(64);
				statement();
				}
				break;
			case T_INT:
				enterOuterAlt(_localctx, 2);
				{
				setState(65);
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
			setState(68);
			basicType();
			setState(69);
			varDef();
			setState(74);
			_errHandler.sync(this);
			_la = _input.LA(1);
			while (_la==T_COMMA) {
				{
				{
				setState(70);
				match(T_COMMA);
				setState(71);
				varDef();
				}
				}
				setState(76);
				_errHandler.sync(this);
				_la = _input.LA(1);
			}
			setState(77);
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
			setState(79);
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
			setState(85); 
			_errHandler.sync(this);
			_la = _input.LA(1);
			do {
				{
				{
				setState(81);
				match(T_L_BRACKET);
				setState(82);
				expr();
				setState(83);
				match(T_R_BRACKET);
				}
				}
				setState(87); 
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
			setState(89);
			match(T_L_BRACE);
			setState(98);
			_errHandler.sync(this);
			_la = _input.LA(1);
			if ((((_la) & ~0x3f) == 0 && ((1L << _la) & ((1L << T_L_PAREN) | (1L << T_L_BRACE) | (1L << T_ID) | (1L << T_DIGIT))) != 0)) {
				{
				setState(90);
				initItem();
				setState(95);
				_errHandler.sync(this);
				_la = _input.LA(1);
				while (_la==T_COMMA) {
					{
					{
					setState(91);
					match(T_COMMA);
					setState(92);
					initItem();
					}
					}
					setState(97);
					_errHandler.sync(this);
					_la = _input.LA(1);
				}
				}
			}

			setState(100);
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
			setState(104);
			_errHandler.sync(this);
			switch (_input.LA(1)) {
			case T_L_BRACE:
				enterOuterAlt(_localctx, 1);
				{
				setState(102);
				initList();
				}
				break;
			case T_L_PAREN:
			case T_ID:
			case T_DIGIT:
				enterOuterAlt(_localctx, 2);
				{
				setState(103);
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
			setState(106);
			match(T_ID);
			setState(108);
			_errHandler.sync(this);
			_la = _input.LA(1);
			if (_la==T_L_BRACKET) {
				{
				setState(107);
				arrayDimensions();
				}
			}

			setState(115);
			_errHandler.sync(this);
			_la = _input.LA(1);
			if (_la==T_ASSIGN) {
				{
				setState(110);
				match(T_ASSIGN);
				setState(113);
				_errHandler.sync(this);
				switch (_input.LA(1)) {
				case T_L_BRACE:
					{
					setState(111);
					initList();
					}
					break;
				case T_L_PAREN:
				case T_ID:
				case T_DIGIT:
					{
					setState(112);
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
			setState(131);
			_errHandler.sync(this);
			switch ( getInterpreter().adaptivePredict(_input,14,_ctx) ) {
			case 1:
				_localctx = new ReturnStatementContext(_localctx);
				enterOuterAlt(_localctx, 1);
				{
				setState(117);
				match(T_RETURN);
				setState(118);
				expr();
				setState(119);
				match(T_SEMICOLON);
				}
				break;
			case 2:
				_localctx = new AssignStatementContext(_localctx);
				enterOuterAlt(_localctx, 2);
				{
				setState(121);
				lVal();
				setState(122);
				match(T_ASSIGN);
				setState(123);
				expr();
				setState(124);
				match(T_SEMICOLON);
				}
				break;
			case 3:
				_localctx = new BlockStatementContext(_localctx);
				enterOuterAlt(_localctx, 3);
				{
				setState(126);
				block();
				}
				break;
			case 4:
				_localctx = new ExpressionStatementContext(_localctx);
				enterOuterAlt(_localctx, 4);
				{
				setState(128);
				_errHandler.sync(this);
				_la = _input.LA(1);
				if ((((_la) & ~0x3f) == 0 && ((1L << _la) & ((1L << T_L_PAREN) | (1L << T_ID) | (1L << T_DIGIT))) != 0)) {
					{
					setState(127);
					expr();
					}
				}

				setState(130);
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
			setState(133);
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
		public List<UnaryExpContext> unaryExp() {
			return getRuleContexts(UnaryExpContext.class);
		}
		public UnaryExpContext unaryExp(int i) {
			return getRuleContext(UnaryExpContext.class,i);
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
			setState(135);
			unaryExp();
			setState(141);
			_errHandler.sync(this);
			_la = _input.LA(1);
			while (_la==T_ADD || _la==T_SUB) {
				{
				{
				setState(136);
				addOp();
				setState(137);
				unaryExp();
				}
				}
				setState(143);
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
			setState(144);
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
		public UnaryExpContext(ParserRuleContext parent, int invokingState) {
			super(parent, invokingState);
		}
		@Override public int getRuleIndex() { return RULE_unaryExp; }
	}

	public final UnaryExpContext unaryExp() throws RecognitionException {
		UnaryExpContext _localctx = new UnaryExpContext(_ctx, getState());
		enterRule(_localctx, 30, RULE_unaryExp);
		int _la;
		try {
			setState(153);
			_errHandler.sync(this);
			switch ( getInterpreter().adaptivePredict(_input,17,_ctx) ) {
			case 1:
				enterOuterAlt(_localctx, 1);
				{
				setState(146);
				primaryExp();
				}
				break;
			case 2:
				enterOuterAlt(_localctx, 2);
				{
				setState(147);
				match(T_ID);
				setState(148);
				match(T_L_PAREN);
				setState(150);
				_errHandler.sync(this);
				_la = _input.LA(1);
				if ((((_la) & ~0x3f) == 0 && ((1L << _la) & ((1L << T_L_PAREN) | (1L << T_ID) | (1L << T_DIGIT))) != 0)) {
					{
					setState(149);
					realParamList();
					}
				}

				setState(152);
				match(T_R_PAREN);
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
		enterRule(_localctx, 32, RULE_primaryExp);
		try {
			setState(161);
			_errHandler.sync(this);
			switch (_input.LA(1)) {
			case T_L_PAREN:
				enterOuterAlt(_localctx, 1);
				{
				setState(155);
				match(T_L_PAREN);
				setState(156);
				expr();
				setState(157);
				match(T_R_PAREN);
				}
				break;
			case T_DIGIT:
				enterOuterAlt(_localctx, 2);
				{
				setState(159);
				match(T_DIGIT);
				}
				break;
			case T_ID:
				enterOuterAlt(_localctx, 3);
				{
				setState(160);
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
		enterRule(_localctx, 34, RULE_realParamList);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(163);
			expr();
			setState(168);
			_errHandler.sync(this);
			_la = _input.LA(1);
			while (_la==T_COMMA) {
				{
				{
				setState(164);
				match(T_COMMA);
				setState(165);
				expr();
				}
				}
				setState(170);
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
		enterRule(_localctx, 36, RULE_lVal);
		int _la;
		try {
			enterOuterAlt(_localctx, 1);
			{
			setState(171);
			match(T_ID);
			setState(178);
			_errHandler.sync(this);
			_la = _input.LA(1);
			while (_la==T_L_BRACKET) {
				{
				{
				setState(172);
				match(T_L_BRACKET);
				setState(173);
				expr();
				setState(174);
				match(T_R_BRACKET);
				}
				}
				setState(180);
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
		"\3\u608b\ua72a\u8133\ub9ed\u417c\u3be7\u7786\u5964\3\25\u00b8\4\2\t\2"+
		"\4\3\t\3\4\4\t\4\4\5\t\5\4\6\t\6\4\7\t\7\4\b\t\b\4\t\t\t\4\n\t\n\4\13"+
		"\t\13\4\f\t\f\4\r\t\r\4\16\t\16\4\17\t\17\4\20\t\20\4\21\t\21\4\22\t\22"+
		"\4\23\t\23\4\24\t\24\3\2\3\2\7\2+\n\2\f\2\16\2.\13\2\3\2\3\2\3\3\3\3\3"+
		"\3\3\3\3\3\3\3\3\4\3\4\5\4:\n\4\3\4\3\4\3\5\6\5?\n\5\r\5\16\5@\3\6\3\6"+
		"\5\6E\n\6\3\7\3\7\3\7\3\7\7\7K\n\7\f\7\16\7N\13\7\3\7\3\7\3\b\3\b\3\t"+
		"\3\t\3\t\3\t\6\tX\n\t\r\t\16\tY\3\n\3\n\3\n\3\n\7\n`\n\n\f\n\16\nc\13"+
		"\n\5\ne\n\n\3\n\3\n\3\13\3\13\5\13k\n\13\3\f\3\f\5\fo\n\f\3\f\3\f\3\f"+
		"\5\ft\n\f\5\fv\n\f\3\r\3\r\3\r\3\r\3\r\3\r\3\r\3\r\3\r\3\r\3\r\5\r\u0083"+
		"\n\r\3\r\5\r\u0086\n\r\3\16\3\16\3\17\3\17\3\17\3\17\7\17\u008e\n\17\f"+
		"\17\16\17\u0091\13\17\3\20\3\20\3\21\3\21\3\21\3\21\5\21\u0099\n\21\3"+
		"\21\5\21\u009c\n\21\3\22\3\22\3\22\3\22\3\22\3\22\5\22\u00a4\n\22\3\23"+
		"\3\23\3\23\7\23\u00a9\n\23\f\23\16\23\u00ac\13\23\3\24\3\24\3\24\3\24"+
		"\3\24\7\24\u00b3\n\24\f\24\16\24\u00b6\13\24\3\24\2\2\25\2\4\6\b\n\f\16"+
		"\20\22\24\26\30\32\34\36 \"$&\2\3\3\2\f\r\2\u00bc\2,\3\2\2\2\4\61\3\2"+
		"\2\2\6\67\3\2\2\2\b>\3\2\2\2\nD\3\2\2\2\fF\3\2\2\2\16Q\3\2\2\2\20W\3\2"+
		"\2\2\22[\3\2\2\2\24j\3\2\2\2\26l\3\2\2\2\30\u0085\3\2\2\2\32\u0087\3\2"+
		"\2\2\34\u0089\3\2\2\2\36\u0092\3\2\2\2 \u009b\3\2\2\2\"\u00a3\3\2\2\2"+
		"$\u00a5\3\2\2\2&\u00ad\3\2\2\2(+\5\4\3\2)+\5\f\7\2*(\3\2\2\2*)\3\2\2\2"+
		"+.\3\2\2\2,*\3\2\2\2,-\3\2\2\2-/\3\2\2\2.,\3\2\2\2/\60\7\2\2\3\60\3\3"+
		"\2\2\2\61\62\7\17\2\2\62\63\7\21\2\2\63\64\7\3\2\2\64\65\7\4\2\2\65\66"+
		"\5\6\4\2\66\5\3\2\2\2\679\7\6\2\28:\5\b\5\298\3\2\2\29:\3\2\2\2:;\3\2"+
		"\2\2;<\7\7\2\2<\7\3\2\2\2=?\5\n\6\2>=\3\2\2\2?@\3\2\2\2@>\3\2\2\2@A\3"+
		"\2\2\2A\t\3\2\2\2BE\5\30\r\2CE\5\f\7\2DB\3\2\2\2DC\3\2\2\2E\13\3\2\2\2"+
		"FG\5\16\b\2GL\5\26\f\2HI\7\13\2\2IK\5\26\f\2JH\3\2\2\2KN\3\2\2\2LJ\3\2"+
		"\2\2LM\3\2\2\2MO\3\2\2\2NL\3\2\2\2OP\7\5\2\2P\r\3\2\2\2QR\7\17\2\2R\17"+
		"\3\2\2\2ST\7\b\2\2TU\5\32\16\2UV\7\t\2\2VX\3\2\2\2WS\3\2\2\2XY\3\2\2\2"+
		"YW\3\2\2\2YZ\3\2\2\2Z\21\3\2\2\2[d\7\6\2\2\\a\5\24\13\2]^\7\13\2\2^`\5"+
		"\24\13\2_]\3\2\2\2`c\3\2\2\2a_\3\2\2\2ab\3\2\2\2be\3\2\2\2ca\3\2\2\2d"+
		"\\\3\2\2\2de\3\2\2\2ef\3\2\2\2fg\7\7\2\2g\23\3\2\2\2hk\5\22\n\2ik\5\32"+
		"\16\2jh\3\2\2\2ji\3\2\2\2k\25\3\2\2\2ln\7\21\2\2mo\5\20\t\2nm\3\2\2\2"+
		"no\3\2\2\2ou\3\2\2\2ps\7\n\2\2qt\5\22\n\2rt\5\32\16\2sq\3\2\2\2sr\3\2"+
		"\2\2tv\3\2\2\2up\3\2\2\2uv\3\2\2\2v\27\3\2\2\2wx\7\16\2\2xy\5\32\16\2"+
		"yz\7\5\2\2z\u0086\3\2\2\2{|\5&\24\2|}\7\n\2\2}~\5\32\16\2~\177\7\5\2\2"+
		"\177\u0086\3\2\2\2\u0080\u0086\5\6\4\2\u0081\u0083\5\32\16\2\u0082\u0081"+
		"\3\2\2\2\u0082\u0083\3\2\2\2\u0083\u0084\3\2\2\2\u0084\u0086\7\5\2\2\u0085"+
		"w\3\2\2\2\u0085{\3\2\2\2\u0085\u0080\3\2\2\2\u0085\u0082\3\2\2\2\u0086"+
		"\31\3\2\2\2\u0087\u0088\5\34\17\2\u0088\33\3\2\2\2\u0089\u008f\5 \21\2"+
		"\u008a\u008b\5\36\20\2\u008b\u008c\5 \21\2\u008c\u008e\3\2\2\2\u008d\u008a"+
		"\3\2\2\2\u008e\u0091\3\2\2\2\u008f\u008d\3\2\2\2\u008f\u0090\3\2\2\2\u0090"+
		"\35\3\2\2\2\u0091\u008f\3\2\2\2\u0092\u0093\t\2\2\2\u0093\37\3\2\2\2\u0094"+
		"\u009c\5\"\22\2\u0095\u0096\7\21\2\2\u0096\u0098\7\3\2\2\u0097\u0099\5"+
		"$\23\2\u0098\u0097\3\2\2\2\u0098\u0099\3\2\2\2\u0099\u009a\3\2\2\2\u009a"+
		"\u009c\7\4\2\2\u009b\u0094\3\2\2\2\u009b\u0095\3\2\2\2\u009c!\3\2\2\2"+
		"\u009d\u009e\7\3\2\2\u009e\u009f\5\32\16\2\u009f\u00a0\7\4\2\2\u00a0\u00a4"+
		"\3\2\2\2\u00a1\u00a4\7\22\2\2\u00a2\u00a4\5&\24\2\u00a3\u009d\3\2\2\2"+
		"\u00a3\u00a1\3\2\2\2\u00a3\u00a2\3\2\2\2\u00a4#\3\2\2\2\u00a5\u00aa\5"+
		"\32\16\2\u00a6\u00a7\7\13\2\2\u00a7\u00a9\5\32\16\2\u00a8\u00a6\3\2\2"+
		"\2\u00a9\u00ac\3\2\2\2\u00aa\u00a8\3\2\2\2\u00aa\u00ab\3\2\2\2\u00ab%"+
		"\3\2\2\2\u00ac\u00aa\3\2\2\2\u00ad\u00b4\7\21\2\2\u00ae\u00af\7\b\2\2"+
		"\u00af\u00b0\5\32\16\2\u00b0\u00b1\7\t\2\2\u00b1\u00b3\3\2\2\2\u00b2\u00ae"+
		"\3\2\2\2\u00b3\u00b6\3\2\2\2\u00b4\u00b2\3\2\2\2\u00b4\u00b5\3\2\2\2\u00b5"+
		"\'\3\2\2\2\u00b6\u00b4\3\2\2\2\27*,9@DLYadjnsu\u0082\u0085\u008f\u0098"+
		"\u009b\u00a3\u00aa\u00b4";
	public static final ATN _ATN =
		new ATNDeserializer().deserialize(_serializedATN.toCharArray());
	static {
		_decisionToDFA = new DFA[_ATN.getNumberOfDecisions()];
		for (int i = 0; i < _ATN.getNumberOfDecisions(); i++) {
			_decisionToDFA[i] = new DFA(_ATN.getDecisionState(i), i);
		}
	}
}