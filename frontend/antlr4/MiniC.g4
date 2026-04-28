grammar MiniC;

// 词法规则名总是以大写字母开头

// 语法规则名总是以小写字母开头

// 每个非终结符尽量多包含闭包、正闭包或可选符等的EBNF范式描述

// 若非终结符由多个产生式组成，则建议在每个产生式的尾部追加# 名称来区分，详细可查看非终结符statement的描述

// 语法规则描述：EBNF范式

// 源文件编译单元定义
compileUnit: (funcDef | decl)* EOF;

// 声明
decl: constDecl | varDecl;

// 函数定义，支持 void/int 返回类型
funcDef: funcType T_ID T_L_PAREN formalParamList? T_R_PAREN block;

// 函数返回类型
funcType: T_INT | T_VOID;

// 形参列表
formalParamList: formalParam (T_COMMA formalParam)*;

// 单个形参，支持标量与数组形参
formalParam: basicType T_ID formalParamDims?;

// 数组形参维度：第一维可以省略
formalParamDims: T_L_BRACK T_R_BRACK (T_L_BRACK expr T_R_BRACK)*;

// 语句块看用作函数体，这里允许多个语句，并且不含任何语句
block: T_L_BRACE blockItemList? T_R_BRACE;

// 每个ItemList可包含至少一个Item
blockItemList: blockItem+;

// 每个Item可以是一个语句，或者声明语句
blockItem: statement | decl;

// 常量声明
constDecl: T_CONST basicType constDef (T_COMMA constDef)* T_SEMICOLON;

// 变量声明，支持变量定义时初始化
varDecl: basicType varDef (T_COMMA varDef)* T_SEMICOLON;

// 常量定义
constDef: T_ID arrayDefDims? T_ASSIGN initVal;

// 基本类型
basicType: T_INT;

// 多维数组声明
arrayDimensions: (T_L_BRACKET expr T_R_BRACKET)+;

// 初始化列表(支持嵌套和空列表)
initList: T_L_BRACE (initItem (T_COMMA initItem)*)? T_R_BRACE;

// 初始化项:嵌套初始化列表或表达式
initItem: initList | expr;

// 变量定义
varDef: T_ID arrayDefDims? (T_ASSIGN initVal)?;

// 数组定义维度
arrayDefDims: (T_L_BRACK expr T_R_BRACK)+;

// 初始化值
initVal: expr | T_L_BRACE (initVal (T_COMMA initVal)*)? T_COMMA? T_R_BRACE;

// 目前语句支持return、赋值、分支与循环
statement:
	T_RETURN expr? T_SEMICOLON			# returnStatement
	| lVal T_ASSIGN expr T_SEMICOLON	# assignStatement
	| T_IF T_L_PAREN expr T_R_PAREN statement (T_ELSE statement)? # ifStatement
	| T_WHILE T_L_PAREN expr T_R_PAREN statement # whileStatement
	| T_BREAK T_SEMICOLON				# breakStatement
	| T_CONTINUE T_SEMICOLON			# continueStatement
	| block								# blockStatement
	| expr? T_SEMICOLON					# expressionStatement;

// 表达式文法 expr : LOrExp
expr: lOrExp;

// 逻辑或表达式
lOrExp: lAndExp (T_LOR lAndExp)*;

// 逻辑与表达式
lAndExp: eqExp (T_LAND eqExp)*;

// 相等性表达式
eqExp: relExp (eqOp relExp)*;

// 相等性运算符
eqOp: T_EQ | T_NE;

// 关系表达式
relExp: addExp (relOp addExp)*;

// 关系运算符
relOp: T_LT | T_GT | T_LE | T_GE;

// 加减表达式
addExp: mulExp (addOp mulExp)*;

// 加减运算符
addOp: T_ADD | T_SUB;

// 乘除求余表达式
mulExp: unaryExp (mulOp unaryExp)*;

// 乘除求余运算符
mulOp: T_MUL | T_DIV | T_MOD;

// 一元表达式
unaryExp: primaryExp | T_ID T_L_PAREN realParamList? T_R_PAREN | unaryOp unaryExp;

// 单目运算符
unaryOp: T_ADD | T_SUB | T_NOT;

// 基本表达式：括号表达式、整数、左值表达式
primaryExp: T_L_PAREN expr T_R_PAREN | T_DIGIT | lVal;

// 实参列表
realParamList: expr (T_COMMA expr)*;

// 左值表达式
lVal: T_ID (T_L_BRACK expr T_R_BRACK)*;

// 用正规式来进行词法规则的描述

T_L_PAREN: '(';
T_R_PAREN: ')';
T_L_BRACK: '[';
T_R_BRACK: ']';
T_SEMICOLON: ';';
T_L_BRACE: '{';
T_R_BRACE: '}';
T_L_BRACKET: '[';
T_R_BRACKET: ']';

T_COMMA: ',';
T_ASSIGN: '=';

T_EQ: '==';
T_NE: '!=';
T_LE: '<=';
T_GE: '>=';
T_LT: '<';
T_GT: '>';

T_ADD: '+';
T_SUB: '-';
T_MUL: '*';
T_DIV: '/';
T_MOD: '%';
T_NOT: '!';
T_LAND: '&&';
T_LOR: '||';

// 要注意关键字同样也属于T_ID，因此必须放在T_ID的前面，否则会识别成T_ID
T_IF: 'if';
T_ELSE: 'else';
T_WHILE: 'while';
T_BREAK: 'break';
T_CONTINUE: 'continue';
T_RETURN: 'return';
T_CONST: 'const';
T_INT: 'int';
T_VOID: 'void';

T_ID: [a-zA-Z_][a-zA-Z0-9_]*;
T_DIGIT: '0' [xX] [0-9a-fA-F]+ | '0' [0-7]* | [1-9][0-9]*;

LINE_COMMENT: '//' ~[\r\n]* -> skip;
BLOCK_COMMENT: '/*' .*? '*/' -> skip;

/* 空白符丢弃 */
WS: [ \r\n\t]+ -> skip;
