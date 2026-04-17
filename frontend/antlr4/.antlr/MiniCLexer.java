// Generated from /home/code/compiler-lite/frontend/antlr4/MiniC.g4 by ANTLR 4.9.2
import org.antlr.v4.runtime.Lexer;
import org.antlr.v4.runtime.CharStream;
import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.TokenStream;
import org.antlr.v4.runtime.*;
import org.antlr.v4.runtime.atn.*;
import org.antlr.v4.runtime.dfa.DFA;
import org.antlr.v4.runtime.misc.*;

@SuppressWarnings({"all", "warnings", "unchecked", "unused", "cast"})
public class MiniCLexer extends Lexer {
	static { RuntimeMetaData.checkVersion("4.9.2", RuntimeMetaData.VERSION); }

	protected static final DFA[] _decisionToDFA;
	protected static final PredictionContextCache _sharedContextCache =
		new PredictionContextCache();
	public static final int
		T_L_PAREN=1, T_R_PAREN=2, T_SEMICOLON=3, T_L_BRACE=4, T_R_BRACE=5, T_L_BRACKET=6, 
		T_R_BRACKET=7, T_ASSIGN=8, T_COMMA=9, T_ADD=10, T_SUB=11, T_MUL=12, T_DIV=13, 
		T_MOD=14, T_RETURN=15, T_INT=16, T_VOID=17, T_ID=18, T_DIGIT=19, LINE_COMMENT=20, 
		BLOCK_COMMENT=21, WS=22;
	public static String[] channelNames = {
		"DEFAULT_TOKEN_CHANNEL", "HIDDEN"
	};

	public static String[] modeNames = {
		"DEFAULT_MODE"
	};

	private static String[] makeRuleNames() {
		return new String[] {
			"T_L_PAREN", "T_R_PAREN", "T_SEMICOLON", "T_L_BRACE", "T_R_BRACE", "T_L_BRACKET", 
			"T_R_BRACKET", "T_ASSIGN", "T_COMMA", "T_ADD", "T_SUB", "T_MUL", "T_DIV", 
			"T_MOD", "T_RETURN", "T_INT", "T_VOID", "T_ID", "T_DIGIT", "LINE_COMMENT", 
			"BLOCK_COMMENT", "WS"
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


	public MiniCLexer(CharStream input) {
		super(input);
		_interp = new LexerATNSimulator(this,_ATN,_decisionToDFA,_sharedContextCache);
	}

	@Override
	public String getGrammarFileName() { return "MiniC.g4"; }

	@Override
	public String[] getRuleNames() { return ruleNames; }

	@Override
	public String getSerializedATN() { return _serializedATN; }

	@Override
	public String[] getChannelNames() { return channelNames; }

	@Override
	public String[] getModeNames() { return modeNames; }

	@Override
	public ATN getATN() { return _ATN; }

	public static final String _serializedATN =
		"\3\u608b\ua72a\u8133\ub9ed\u417c\u3be7\u7786\u5964\2\30\u009a\b\1\4\2"+
		"\t\2\4\3\t\3\4\4\t\4\4\5\t\5\4\6\t\6\4\7\t\7\4\b\t\b\4\t\t\t\4\n\t\n\4"+
		"\13\t\13\4\f\t\f\4\r\t\r\4\16\t\16\4\17\t\17\4\20\t\20\4\21\t\21\4\22"+
		"\t\22\4\23\t\23\4\24\t\24\4\25\t\25\4\26\t\26\4\27\t\27\3\2\3\2\3\3\3"+
		"\3\3\4\3\4\3\5\3\5\3\6\3\6\3\7\3\7\3\b\3\b\3\t\3\t\3\n\3\n\3\13\3\13\3"+
		"\f\3\f\3\r\3\r\3\16\3\16\3\17\3\17\3\20\3\20\3\20\3\20\3\20\3\20\3\20"+
		"\3\21\3\21\3\21\3\21\3\22\3\22\3\22\3\22\3\22\3\23\3\23\7\23^\n\23\f\23"+
		"\16\23a\13\23\3\24\3\24\3\24\7\24f\n\24\f\24\16\24i\13\24\3\24\3\24\3"+
		"\24\6\24n\n\24\r\24\16\24o\3\24\3\24\3\24\6\24u\n\24\r\24\16\24v\5\24"+
		"y\n\24\3\25\3\25\3\25\3\25\7\25\177\n\25\f\25\16\25\u0082\13\25\3\25\3"+
		"\25\3\26\3\26\3\26\3\26\7\26\u008a\n\26\f\26\16\26\u008d\13\26\3\26\3"+
		"\26\3\26\3\26\3\26\3\27\6\27\u0095\n\27\r\27\16\27\u0096\3\27\3\27\3\u008b"+
		"\2\30\3\3\5\4\7\5\t\6\13\7\r\b\17\t\21\n\23\13\25\f\27\r\31\16\33\17\35"+
		"\20\37\21!\22#\23%\24\'\25)\26+\27-\30\3\2\n\5\2C\\aac|\6\2\62;C\\aac"+
		"|\3\2\63;\3\2\62;\4\2ZZzz\5\2\62;CHch\4\2\f\f\17\17\5\2\13\f\17\17\"\""+
		"\2\u00a3\2\3\3\2\2\2\2\5\3\2\2\2\2\7\3\2\2\2\2\t\3\2\2\2\2\13\3\2\2\2"+
		"\2\r\3\2\2\2\2\17\3\2\2\2\2\21\3\2\2\2\2\23\3\2\2\2\2\25\3\2\2\2\2\27"+
		"\3\2\2\2\2\31\3\2\2\2\2\33\3\2\2\2\2\35\3\2\2\2\2\37\3\2\2\2\2!\3\2\2"+
		"\2\2#\3\2\2\2\2%\3\2\2\2\2\'\3\2\2\2\2)\3\2\2\2\2+\3\2\2\2\2-\3\2\2\2"+
		"\3/\3\2\2\2\5\61\3\2\2\2\7\63\3\2\2\2\t\65\3\2\2\2\13\67\3\2\2\2\r9\3"+
		"\2\2\2\17;\3\2\2\2\21=\3\2\2\2\23?\3\2\2\2\25A\3\2\2\2\27C\3\2\2\2\31"+
		"E\3\2\2\2\33G\3\2\2\2\35I\3\2\2\2\37K\3\2\2\2!R\3\2\2\2#V\3\2\2\2%[\3"+
		"\2\2\2\'x\3\2\2\2)z\3\2\2\2+\u0085\3\2\2\2-\u0094\3\2\2\2/\60\7*\2\2\60"+
		"\4\3\2\2\2\61\62\7+\2\2\62\6\3\2\2\2\63\64\7=\2\2\64\b\3\2\2\2\65\66\7"+
		"}\2\2\66\n\3\2\2\2\678\7\177\2\28\f\3\2\2\29:\7]\2\2:\16\3\2\2\2;<\7_"+
		"\2\2<\20\3\2\2\2=>\7?\2\2>\22\3\2\2\2?@\7.\2\2@\24\3\2\2\2AB\7-\2\2B\26"+
		"\3\2\2\2CD\7/\2\2D\30\3\2\2\2EF\7,\2\2F\32\3\2\2\2GH\7\61\2\2H\34\3\2"+
		"\2\2IJ\7\'\2\2J\36\3\2\2\2KL\7t\2\2LM\7g\2\2MN\7v\2\2NO\7w\2\2OP\7t\2"+
		"\2PQ\7p\2\2Q \3\2\2\2RS\7k\2\2ST\7p\2\2TU\7v\2\2U\"\3\2\2\2VW\7x\2\2W"+
		"X\7q\2\2XY\7k\2\2YZ\7f\2\2Z$\3\2\2\2[_\t\2\2\2\\^\t\3\2\2]\\\3\2\2\2^"+
		"a\3\2\2\2_]\3\2\2\2_`\3\2\2\2`&\3\2\2\2a_\3\2\2\2by\7\62\2\2cg\t\4\2\2"+
		"df\t\5\2\2ed\3\2\2\2fi\3\2\2\2ge\3\2\2\2gh\3\2\2\2hy\3\2\2\2ig\3\2\2\2"+
		"jk\7\62\2\2km\t\4\2\2ln\t\5\2\2ml\3\2\2\2no\3\2\2\2om\3\2\2\2op\3\2\2"+
		"\2py\3\2\2\2qr\7\62\2\2rt\t\6\2\2su\t\7\2\2ts\3\2\2\2uv\3\2\2\2vt\3\2"+
		"\2\2vw\3\2\2\2wy\3\2\2\2xb\3\2\2\2xc\3\2\2\2xj\3\2\2\2xq\3\2\2\2y(\3\2"+
		"\2\2z{\7\61\2\2{|\7\61\2\2|\u0080\3\2\2\2}\177\n\b\2\2~}\3\2\2\2\177\u0082"+
		"\3\2\2\2\u0080~\3\2\2\2\u0080\u0081\3\2\2\2\u0081\u0083\3\2\2\2\u0082"+
		"\u0080\3\2\2\2\u0083\u0084\b\25\2\2\u0084*\3\2\2\2\u0085\u0086\7\61\2"+
		"\2\u0086\u0087\7,\2\2\u0087\u008b\3\2\2\2\u0088\u008a\13\2\2\2\u0089\u0088"+
		"\3\2\2\2\u008a\u008d\3\2\2\2\u008b\u008c\3\2\2\2\u008b\u0089\3\2\2\2\u008c"+
		"\u008e\3\2\2\2\u008d\u008b\3\2\2\2\u008e\u008f\7,\2\2\u008f\u0090\7\61"+
		"\2\2\u0090\u0091\3\2\2\2\u0091\u0092\b\26\2\2\u0092,\3\2\2\2\u0093\u0095"+
		"\t\t\2\2\u0094\u0093\3\2\2\2\u0095\u0096\3\2\2\2\u0096\u0094\3\2\2\2\u0096"+
		"\u0097\3\2\2\2\u0097\u0098\3\2\2\2\u0098\u0099\b\27\2\2\u0099.\3\2\2\2"+
		"\13\2_govx\u0080\u008b\u0096\3\b\2\2";
	public static final ATN _ATN =
		new ATNDeserializer().deserialize(_serializedATN.toCharArray());
	static {
		_decisionToDFA = new DFA[_ATN.getNumberOfDecisions()];
		for (int i = 0; i < _ATN.getNumberOfDecisions(); i++) {
			_decisionToDFA[i] = new DFA(_ATN.getDecisionState(i), i);
		}
	}
}