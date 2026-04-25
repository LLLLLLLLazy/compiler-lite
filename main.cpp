/**
 * @file main.cpp
 * @author zenglj (zenglj@nwpu.edu.cn)
 * @brief 主程序文件
 * @version 0.1
 * @date 2023-09-24
 *
 * @copyright Copyright (c) 2023
 *
 */

#include <iostream>
#include <fstream>
#include <string>
#include <getopt.h>

#ifdef _WIN32
#include <Windows.h>
#endif

#include "Common.h"
#include "AST.h"
#include "Antlr4Executor.h"
#include "FrontEndExecutor.h"
#include "Graph.h"
#include "IRGenerator.h"
#include "LLVMIREmitter.h"
#include "Module.h"
#include "DominatorTree.h"
#include "DominanceFrontier.h"
#include "DCE.h"
#include "Mem2Reg.h"
#include "PhiLowering.h"
#include "CodeGeneratorRiscV64.h"

///
/// @brief 是否显示帮助信息
///
static bool gShowHelp = false;

///
/// @brief 显示抽象语法树
///
static bool gShowAST = false;

///
/// @brief 输出结构化 IR，供调试使用
///
static bool gShowStructIR = false;

///
/// @brief 产生LLVM IR文本，默认输出
///
static bool gShowLLVMIR = false;

///
/// @brief 输出支配树与支配边界分析结果
///
static bool gShowDomInfo = false;

///
/// @brief 输出中间结果，含结构化IR、LLVM IR等
///
static bool gShowSymbol = false;

///
/// @brief 前端分析器，默认选Flex和Bison
///
static bool gFrontEndFlexBison = true;

///
/// @brief 前端分析器Antlr4，是否选中
///
static bool gFrontEndAntlr4 = false;

///
/// @brief 前端分析器用递归下降分析法，是否选中
///
static bool gFrontEndRecursiveDescentParsing = false;

///
/// @brief 预留给后端汇编输出时作为注释显示IR
///
static bool gAsmAlsoShowIR = false;

///
/// @brief 优化的级别，即-O后面的数字，默认为0
///
static int gOptLevel = 0;

///
/// @brief 指定CPU目标架构，默认为RISCV64
///
static std::string gCPUTarget = "RISCV64";

///
/// @brief 输入源文件
///
static std::string gInputFile;

///
/// @brief 输出文件，不同的选项输出的内容不同
///
static std::string gOutputFile;

static struct option long_options[] = {
	{"help", no_argument, nullptr, 'h'},
	{"output", required_argument, nullptr, 'o'},
	{"symbol", no_argument, nullptr, 'S'},
	{"ast", no_argument, nullptr, 'T'},
	{"ir", no_argument, nullptr, 'I'},
	{"llvmir", no_argument, nullptr, 'L'},
	{"antlr4", no_argument, nullptr, 'A'},
	{"recursive-descent", no_argument, nullptr, 'D'},
	{"optimize", required_argument, nullptr, 'O'},
	{"target", required_argument, nullptr, 't'},
	{"asmir", no_argument, nullptr, 'c'},
	{"dom", no_argument, nullptr, 'm'},
	{nullptr, 0, nullptr, 0}};

/// @brief 显示帮助
/// @param exeName
static void showHelp(const std::string & exeName)
{
	std::cout << exeName + " -S [-T | --ast | -I | --ir | -L | --llvmir] [-o output | --output=output] source\n";
	std::cout << "Options:\n";
	std::cout << "  -h, --help                 Show this help message\n";
	std::cout << "  -o, --output=FILE          Specify output file\n";
	std::cout << "  -S, --symbol               Show symbol information\n";
	std::cout << "  -T, --ast                  Output abstract syntax tree\n";
	std::cout << "  -I, --ir                   Output structured IR\n";
	std::cout << "  -L, --llvmir               Output LLVM IR (.ll)\n";
	std::cout << "  -A, --antlr4               Deprecated, now always use Antlr4\n";
	std::cout << "  -D, --recursive-descent    Deprecated, now always use Antlr4\n";
	std::cout << "  -O, --optimize=LEVEL       Set optimization level\n";
	std::cout << "  -t, --target=CPU           Specify target CPU architecture\n";
	std::cout << "  -c, --asmir                Show IR instructions as comments in assembly output\n";
	std::cout << "  --dom                      Output dominator tree and dominance frontier\n";
}

/// @brief 参数解析与有效性检查
/// @param argc
/// @param argv
/// @return
static int ArgsAnalysis(int argc, char * argv[])
{
	int ch;

	// 指定参数解析的选项，可识别-h、-o、-S、-T、-I、-A、-D等选项
	// -S必须项，输出中间IR、抽象语法树或汇编
	// -T指定时输出AST，-I输出中间IR，不指定则默认输出汇编
	// -A和-D已废弃，默认选用Antlr4进行词法语法分析，保留参数兼容性但不生效
	// -o要求必须带有附加参数，指定输出的文件
	// -O要求必须带有附加整数，指明优化的级别
	// -t要求必须带有目标CPU，指明目标CPU的汇编
	// -c选项在输出汇编时有效，附带输出IR指令内容
	const char options[] = "ho:STIADLO:t:cm";
	int option_index = 0;

	opterr = 1;

lb_check:
	while ((ch = getopt_long(argc, argv, options, long_options, &option_index)) != -1) {
		switch (ch) {
			case 'h':
				gShowHelp = true;
				break;
			case 'o':
				gOutputFile = optarg;
				break;
			case 'S':
				gShowSymbol = true;
				break;
			case 'T':
				gShowAST = true;
				break;
			case 'I':
				// 输出结构化 IR
				gShowStructIR = true;
				break;
			case 'L':
				// 产生LLVM IR
				gShowLLVMIR = true;
				break;
			case 'A':
				// 选用antlr4
				gFrontEndAntlr4 = true;
				gFrontEndFlexBison = false;
				gFrontEndRecursiveDescentParsing = false;
				break;
			case 'D':
				// 选用递归下降分析法与词法手动实现
				gFrontEndAntlr4 = false;
				gFrontEndFlexBison = false;
				gFrontEndRecursiveDescentParsing = true;
				break;
			case 'O':
				// 优化级别分析，暂时没有用，如开启优化时请使用
				gOptLevel = std::stoi(optarg);
				break;
			case 't':
				gCPUTarget = optarg;
				break;
			case 'c':
				gAsmAlsoShowIR = true;
				break;
			case 'm':
				gShowDomInfo = true;
				break;
			default:
				return -1;
				break; /* no break */
		}
	}

	argc -= optind;
	argv += optind;

	if (argc >= 1) {

		// 第一次设置
		if (gInputFile.empty()) {

			gInputFile = argv[0];
		} else {
			// 重复设置则出错
			return -1;
		}

		if (argc > 1) {
			// 多余一个参数，则说明输入的源文件后仍然有参数要解析
			optind = 0;
			goto lb_check;
		}
	}

	// 必须指定要进行编译的输入文件
	if (gInputFile.empty()) {
		return -1;
	}

	// 显示符号信息，必须指定，可选抽象语法树、LLVM IR等显示
	if (!gShowSymbol) {
		return -1;
	}

	int flag = (int) gShowStructIR + (int) gShowLLVMIR + (int) gShowAST + (int) gShowDomInfo;

	if (0 == flag) {
		// 没有指定，则默认输出汇编代码
		// 不设置任何标志，让程序走到汇编生成路径
	} else if (flag != 1) {
		// 结构化IR、LLVM IR、抽象语法树、支配树信息只能同时选择一个
		return -1;
	}

	// 没有指定输出文件则产生默认文件
	if (gOutputFile.empty()) {

		// 默认文件名
		if (gShowAST) {
			gOutputFile = "output.png";
		} else if (gShowStructIR) {
			gOutputFile = "output.ir";
		} else if (gShowDomInfo) {
			gOutputFile = "output.dom";
		} else if (gShowLLVMIR) {
			gOutputFile = "output.ll";
		} else {
			// 默认输出汇编文件
			gOutputFile = "output.s";
		}
	}

	return 0;
}

///
/// @brief 对源文件进行编译处理生成汇编
/// @return true 成功
/// @return false 失败
///
static int compile(std::string inputFile, std::string outputFile)
{
	// 函数返回值，默认-1
	int result = -1;

	// 内部函数调用返回值保存变量
	int subResult;
	Module * module = nullptr;

	do {
		// 创建词法语法分析器
		FrontEndExecutor * frontEndExecutor = new Antlr4Executor(inputFile);

		// 前端执行：词法分析、语法分析后产生抽象语法树
		subResult = frontEndExecutor->run();
		if (!subResult) {
			minic_log(LOG_ERROR, "前端分析错误");
			break;
		}

		// 获取抽象语法树的根节点
		ast_node * astRoot = frontEndExecutor->getASTRoot();

		// 清理前端资源
		delete frontEndExecutor;

		// 显示抽象语法树（使用-T或--ast选项时）
		if (gShowAST) {
			OutputAST(astRoot, outputFile);
			ast_node::Delete(astRoot);
			result = 0;
			break;
		}

		// 生成结构化 IR
		module = new Module(inputFile);
		IRGenerator irGenerator(astRoot, module);
		subResult = irGenerator.run();
		if (!subResult) {
			minic_log(LOG_ERROR, "结构化IR生成错误");
			ast_node::Delete(astRoot);
			break;
		}

		module->renameIR();

		// 结构化IR已经生成完成，后续输出不再依赖AST
		ast_node::Delete(astRoot);

		if (gShowStructIR) {
			module->outputIR(outputFile);
			result = 0;
			break;
		}

		// 输出支配树与支配边界分析结果（使用--dom选项时）
		if (gShowDomInfo) {
			std::string domOutput;
			for (auto * func : module->getFunctionList()) {
				if (func->isBuiltin() || func->getBlocks().empty()) {
					continue;
				}
				domOutput += "Function: " + func->getName() + "\n";
				DominatorTree dt(func);
				dt.print(domOutput);
				DominanceFrontier df(func, dt);
				df.print(domOutput);
				domOutput += "\n";
			}
			std::ofstream domFile(outputFile, std::ios::out | std::ios::trunc);
			if (!domFile.is_open()) {
				minic_log(LOG_ERROR, "支配树输出文件打开失败");
				break;
			}
			domFile << domOutput;
			result = domFile.good() ? 0 : -1;
			break;
		}

		// 运行 mem2reg，将可提升的 alloca/load/store 转为 SSA 形式
		for (auto * func : module->getFunctionList()) {
			if (!func->isBuiltin() && !func->getBlocks().empty()) {
				Mem2Reg mem2reg(func, module);
				mem2reg.run();
			}
		}

		// 运行 DCE，删除死代码
		for (auto * func : module->getFunctionList()) {
			if (!func->isBuiltin() && !func->getBlocks().empty()) {
				DCE dce(func);
				dce.run();
			}
		}

		// LLVM IR输出路径（使用-L参数时）
		if (gShowLLVMIR) {
			module->renameIR();

			LLVMIREmitter irEmitter(module, inputFile);
			subResult = irEmitter.run();
			if (!subResult) {
				minic_log(LOG_ERROR, "LLVM IR生成错误");
				break;
			}

			// 确定LLVM IR输出文件名
			std::string llvmIRFile = outputFile;
			size_t dotPos = outputFile.rfind('.');
			if (dotPos != std::string::npos) {
				llvmIRFile = outputFile.substr(0, dotPos) + ".ll";
			} else {
				llvmIRFile = outputFile + ".ll";
			}

			// 输出LLVM IR文本
			if (!irEmitter.writeToFile(llvmIRFile)) {
				minic_log(LOG_ERROR, "LLVM IR输出错误");
				break;
			}

			minic_log(LOG_INFO, "LLVM IR已输出到: %s", llvmIRFile.c_str());
			result = 0;
			break;
		}

		// 检查目标CPU架构是否支持
		if (!gCPUTarget.empty() && gCPUTarget != "RISCV64") {
			minic_log(LOG_ERROR, "指定的目标CPU架构(%s)不支持", gCPUTarget.c_str());
			break;
		}

		// 对每个非内建函数执行Phi降级（将phi节点转换为copy指令）
		for (auto * func : module->getFunctionList()) {
			if (!func->isBuiltin() && !func->getBlocks().empty()) {
				PhiLowering phiLowering(func, module);
				phiLowering.run();
			}
		}

		// 重新编号IR名称
		module->renameIR();

		// 确定汇编输出文件名
		std::string asmOutputFile = outputFile;
		size_t dotPos = outputFile.rfind('.');
		if (dotPos != std::string::npos) {
			asmOutputFile = outputFile.substr(0, dotPos) + ".s";
		} else {
			asmOutputFile = outputFile + ".s";
		}

		// 使用RISCV64代码生成器生成汇编
		CodeGeneratorRiscV64 generator(module);
		generator.setShowLinearIR(gAsmAlsoShowIR);
		if (!generator.run(asmOutputFile)) {
			minic_log(LOG_ERROR, "RISCV64汇编生成错误");
			break;
		}

		minic_log(LOG_INFO, "RISCV64汇编已输出到: %s", asmOutputFile.c_str());
		result = 0;
		break;

	} while (false);

	if (module) {
		module->Delete();
		delete module;
	}

	return result;
}

/// @brief 主程序
/// @param argc
/// @param argv
/// @return compile的执行结果，0表示成功，非0表示失败
int main(int argc, char * argv[])
{
	// 函数返回值，默认-1
	int result = -1;

#ifdef _WIN32
	SetConsoleOutputCP(65001);
#endif

	// 参数解析
	result = ArgsAnalysis(argc, argv);
	if (result < 0) {
		// 如果参数解析失败，显示帮助信息
		showHelp(argv[0]);
		return -1;
	}

	// 显示帮助
	if (gShowHelp) {
		showHelp(argv[0]);
		return 0;
	}

	// 如果使用了过时的参数
	if (gFrontEndAntlr4 || gFrontEndRecursiveDescentParsing) {
		minic_log(LOG_INFO, "Warnning: 参数-A/--antlr4和-D/--recursive-descent已废弃，默认选用Antlr4进行前端分析，保留参数兼容性但不生效");
	}

	// 参数解析正确，进行编译处理，目前只支持一个文件的编译。
	result = compile(gInputFile, gOutputFile);

	return result;
}
