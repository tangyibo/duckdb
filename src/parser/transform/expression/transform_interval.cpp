#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/transformer.hpp"
#include "duckdb/common/operator/cast_operators.hpp"

namespace duckdb {
using namespace std;
using namespace duckdb_libpgquery;

unique_ptr<ParsedExpression> Transformer::TransformInterval(PGIntervalConstant *node) {
	// handle post-fix notation of INTERVAL

	// three scenarios
	// interval (expr) year
	// interval 'string' year
	// interval int year
	unique_ptr<ParsedExpression> expr;
	switch(node->val_type) {
	case T_PGAExpr:
		expr = TransformExpression(node->eval);
		break;
	case T_PGString:
		expr = make_unique<ConstantExpression>(Value(node->sval));
		break;
	case T_PGInteger:
		expr = make_unique<ConstantExpression>(Value(node->ival));
		break;
	default:
		throw ParserException("Unsupported interval transformation");
	}

	if (!node->typmods) {
		return make_unique<CastExpression>(LogicalType::INTERVAL, move(expr));
	}

	int mask = ((PGAConst *)node->typmods->head->data.ptr_value)->val.val.ival;
	// these seemingly random constants are from datetime.hpp
	// they are copied here to avoid having to include this header
	// the bitshift is from the function INTERVAL_MASK in the parser
	constexpr int MONTH_MASK = 1 << 1;
	constexpr int YEAR_MASK = 1 << 2;
	constexpr int DAY_MASK = 1 << 3;
	constexpr int HOUR_MASK = 1 << 10;
	constexpr int MINUTE_MASK = 1 << 11;
	constexpr int SECOND_MASK = 1 << 12;

	// we need to check certain combinations
	// because certain interval masks (e.g. INTERVAL '10' HOURS TO DAYS) set multiple bits
	// for now we don't support all of the combined ones
	// (we might add support if someone complains about it)

	string fname;
	LogicalType target_type;
	if (mask & YEAR_MASK && mask & MONTH_MASK) {
		// DAY TO HOUR
		throw ParserException("YEAR TO MONTH is not supported");
	} else if (mask & DAY_MASK && mask & HOUR_MASK) {
		// DAY TO HOUR
		throw ParserException("DAY TO HOUR is not supported");
	} else if (mask & DAY_MASK && mask & MINUTE_MASK) {
		// DAY TO MINUTE
		throw ParserException("DAY TO MINUTE is not supported");
	} else if (mask & DAY_MASK && mask & SECOND_MASK) {
		// DAY TO SECOND
		throw ParserException("DAY TO SECOND is not supported");
	} else if (mask & HOUR_MASK && mask & MINUTE_MASK) {
		// DAY TO SECOND
		throw ParserException("HOUR TO MINUTE is not supported");
	} else if (mask & HOUR_MASK && mask & SECOND_MASK) {
		// DAY TO SECOND
		throw ParserException("HOUR TO SECOND is not supported");
	} else if (mask & MINUTE_MASK && mask & SECOND_MASK) {
		// DAY TO SECOND
		throw ParserException("MINUTE TO SECOND is not supported");
	} else if (mask & YEAR_MASK) {
		// YEAR
		fname = "to_years";
		target_type = LogicalType::INTEGER;
	} else if (mask & MONTH_MASK) {
		// MONTH
		fname = "to_months";
		target_type = LogicalType::INTEGER;
	} else if (mask & DAY_MASK) {
		// DAY
		fname = "to_days";
		target_type = LogicalType::INTEGER;
	} else if (mask & HOUR_MASK) {
		// HOUR
		fname = "to_hours";
		target_type = LogicalType::BIGINT;
	} else if (mask & MINUTE_MASK) {
		// MINUTE
		fname = "to_minutes";
		target_type = LogicalType::BIGINT;
	} else if (mask & SECOND_MASK) {
		// SECOND
		fname = "to_seconds";
		target_type = LogicalType::BIGINT;
	} else {
		throw ParserException("Unsupported interval post-fix");
	}
	// first push a cast to the target type
	expr = make_unique<CastExpression>(target_type, move(expr));
	// now push the operation
	vector<unique_ptr<ParsedExpression>> children;
	children.push_back(move(expr));
	return make_unique<FunctionExpression>(fname, children);
}

}
