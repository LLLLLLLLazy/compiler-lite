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
		T_R_BRACKET=7, T_ASSIGN=8, T_COMMA=9, T_ADD=10, T_SUB=11, T_RETURN=12, 
		T_INT=13, T_VOID=14, T_ID=15, T_DIGIT=16, LINE_COMMENT=17, BLOCK_COMMENT=18, 
		WS=19;
	public static String[] channelNames = {
		"DEFAULT_TOKEN_CHANNEL", "HIDDEN"
	};

	public static String[] modeNames = {
		"DEFAULT_MODE"
	};

	private static String[] makeRuleNames() {
		return new String[] {
			"T_L_PAREN", "T_R_PAREN", "T_SEMICOLON", "T_L_BRACE", "T_R_BRACE", "T_L_BRACKET", 
			"T_R_BRACKET", "T_ASSIGN", "T_COMMA", "T_ADD", "T_SUB", "T_RETURN", "T_INT", 
			"T_VOID", "T_ID", "T_DIGIT", "LINE_COMMENT", "BLOCK_COMMENT", "WS"
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
		"\3\u608b\ua72a\u8133\ub9ed\u417c\u3be7\u7786\u5964\2\25\u0080\b\1\4\2"+
		"\t\2\4\3\t\3\4\4\t\4\4\5\t\5\4\6\t\6\4\7\t\7\4\b\t\b\4\t\t\t\4\n\t\n\4"+
		"\13\t\13\4\f\t\f\4\r\t\r\4\16\t\16\4\17\t\17\4\20\t\20\4\21\t\21\4\22"+
		"\t\22\4\23\t\23\4\24\t\24\3\2\3\2\3\3\3\3\3\4\3\4\3\5\3\5\3\6\3\6\3\7"+
		"\3\7\3\b\3\b\3\t\3\t\3\n\3\n\3\13\3\13\3\f\3\f\3\r\3\r\3\r\3\r\3\r\3\r"+
		"\3\r\3\16\3\16\3\16\3\16\3\17\3\17\3\17\3\17\3\17\3\20\3\20\7\20R\n\20"+
		"\f\20\16\20U\13\20\3\21\3\21\3\21\7\21Z\n\21\f\21\16\21]\13\21\5\21_\n"+
		"\21\3\22\3\22\3\22\3\22\7\22e\n\22\f\22\16\22h\13\22\3\22\3\22\3\23\3"+
		"\23\3\23\3\23\7\23p\n\23\f\23\16\23s\13\23\3\23\3\23\3\23\3\23\3\23\3"+
		"\24\6\24{\n\24\r\24\16\24|\3\24\3\24\3q\2\25\3\3\5\4\7\5\t\6\13\7\r\b"+
		"\17\t\21\n\23\13\25\f\27\r\31\16\33\17\35\20\37\21!\22#\23%\24\'\25\3"+
		"\2\b\5\2C\\aac|\6\2\62;C\\aac|\3\2\63;\3\2\62;\4\2\f\f\17\17\5\2\13\f"+
		"\17\17\"\"\2\u0085\2\3\3\2\2\2\2\5\3\2\2\2\2\7\3\2\2\2\2\t\3\2\2\2\2\13"+
		"\3\2\2\2\2\r\3\2\2\2\2\17\3\2\2\2\2\21\3\2\2\2\2\23\3\2\2\2\2\25\3\2\2"+
		"\2\2\27\3\2\2\2\2\31\3\2\2\2\2\33\3\2\2\2\2\35\3\2\2\2\2\37\3\2\2\2\2"+
		"!\3\2\2\2\2#\3\2\2\2\2%\3\2\2\2\2\'\3\2\2\2\3)\3\2\2\2\5+\3\2\2\2\7-\3"+
		"\2\2\2\t/\3\2\2\2\13\61\3\2\2\2\r\63\3\2\2\2\17\65\3\2\2\2\21\67\3\2\2"+
		"\2\239\3\2\2\2\25;\3\2\2\2\27=\3\2\2\2\31?\3\2\2\2\33F\3\2\2\2\35J\3\2"+
		"\2\2\37O\3\2\2\2!^\3\2\2\2#`\3\2\2\2%k\3\2\2\2\'z\3\2\2\2)*\7*\2\2*\4"+
		"\3\2\2\2+,\7+\2\2,\6\3\2\2\2-.\7=\2\2.\b\3\2\2\2/\60\7}\2\2\60\n\3\2\2"+
		"\2\61\62\7\177\2\2\62\f\3\2\2\2\63\64\7]\2\2\64\16\3\2\2\2\65\66\7_\2"+
		"\2\66\20\3\2\2\2\678\7?\2\28\22\3\2\2\29:\7.\2\2:\24\3\2\2\2;<\7-\2\2"+
		"<\26\3\2\2\2=>\7/\2\2>\30\3\2\2\2?@\7t\2\2@A\7g\2\2AB\7v\2\2BC\7w\2\2"+
		"CD\7t\2\2DE\7p\2\2E\32\3\2\2\2FG\7k\2\2GH\7p\2\2HI\7v\2\2I\34\3\2\2\2"+
		"JK\7x\2\2KL\7q\2\2LM\7k\2\2MN\7f\2\2N\36\3\2\2\2OS\t\2\2\2PR\t\3\2\2Q"+
		"P\3\2\2\2RU\3\2\2\2SQ\3\2\2\2ST\3\2\2\2T \3\2\2\2US\3\2\2\2V_\7\62\2\2"+
		"W[\t\4\2\2XZ\t\5\2\2YX\3\2\2\2Z]\3\2\2\2[Y\3\2\2\2[\\\3\2\2\2\\_\3\2\2"+
		"\2][\3\2\2\2^V\3\2\2\2^W\3\2\2\2_\"\3\2\2\2`a\7\61\2\2ab\7\61\2\2bf\3"+
		"\2\2\2ce\n\6\2\2dc\3\2\2\2eh\3\2\2\2fd\3\2\2\2fg\3\2\2\2gi\3\2\2\2hf\3"+
		"\2\2\2ij\b\22\2\2j$\3\2\2\2kl\7\61\2\2lm\7,\2\2mq\3\2\2\2np\13\2\2\2o"+
		"n\3\2\2\2ps\3\2\2\2qr\3\2\2\2qo\3\2\2\2rt\3\2\2\2sq\3\2\2\2tu\7,\2\2u"+
		"v\7\61\2\2vw\3\2\2\2wx\b\23\2\2x&\3\2\2\2y{\t\7\2\2zy\3\2\2\2{|\3\2\2"+
		"\2|z\3\2\2\2|}\3\2\2\2}~\3\2\2\2~\177\b\24\2\2\177(\3\2\2\2\t\2S[^fq|"+
		"\3\b\2\2";
	public static final ATN _ATN =
		new ATNDeserializer().deserialize(_serializedATN.toCharArray());
	static {
		_decisionToDFA = new DFA[_ATN.getNumberOfDecisions()];
		for (int i = 0; i < _ATN.getNumberOfDecisions(); i++) {
			_decisionToDFA[i] = new DFA(_ATN.getDecisionState(i), i);
		}
	}
}