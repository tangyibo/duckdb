#include "duckdb/function/aggregate/algebraic_functions.hpp"
#include "duckdb/function/aggregate/sum_helpers.hpp"
#include "duckdb/common/types/hugeint.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/planner/expression.hpp"

namespace duckdb {
using namespace std;

template <class T> struct avg_state_t {
	T value;
	uint64_t count;
};

struct AverageDecimalBindData : public FunctionData {
	AverageDecimalBindData(double scale) : scale(scale) {
	}

	double scale;

public:
	unique_ptr<FunctionData> Copy() override {
		return make_unique<AverageDecimalBindData>(scale);
	};
};

struct AverageSetOperation {
	template <class STATE> static void Initialize(STATE *state) {
		state->count = 0;
	}
	template <class STATE> static void Combine(STATE source, STATE *target) {
		target->count += source.count;
		target->value += source.value;
	}
	template <class STATE> static void AddValues(STATE *state, idx_t count) {
		state->count += count;
	}
};

static double GetAverageDivident(uint64_t count, FunctionData *bind_data) {
	double divident = double(count);
	if (bind_data) {
		auto &avg_bind_data = (AverageDecimalBindData &)*bind_data;
		divident *= avg_bind_data.scale;
	}
	return divident;
}

struct IntegerAverageOperation : public BaseSumOperation<AverageSetOperation, RegularAdd> {
	template <class T, class STATE>
	static void Finalize(Vector &result, FunctionData *bind_data, STATE *state, T *target, nullmask_t &nullmask,
	                     idx_t idx) {
		if (state->count == 0) {
			nullmask[idx] = true;
		} else {
			double divident = GetAverageDivident(state->count, bind_data);
			target[idx] = double(state->value) / divident;
		}
	}
};

struct IntegerAverageOperationHugeint : public BaseSumOperation<AverageSetOperation, HugeintAdd> {
	template <class T, class STATE>
	static void Finalize(Vector &result, FunctionData *bind_data, STATE *state, T *target, nullmask_t &nullmask,
	                     idx_t idx) {
		if (state->count == 0) {
			nullmask[idx] = true;
		} else {
			double divident = GetAverageDivident(state->count, bind_data);
			target[idx] = Hugeint::Cast<double>(state->value) / divident;
		}
	}
};

struct HugeintAverageOperation : public BaseSumOperation<AverageSetOperation, RegularAdd> {
	template <class T, class STATE>
	static void Finalize(Vector &result, FunctionData *bind_data, STATE *state, T *target, nullmask_t &nullmask,
	                     idx_t idx) {
		if (state->count == 0) {
			nullmask[idx] = true;
		} else {
			double divident = GetAverageDivident(state->count, bind_data);
			target[idx] = Hugeint::Cast<double>(state->value) / divident;
		}
	}
};

struct NumericAverageOperation : public BaseSumOperation<AverageSetOperation, RegularAdd> {
	template <class T, class STATE>
	static void Finalize(Vector &result, FunctionData *, STATE *state, T *target, nullmask_t &nullmask, idx_t idx) {
		if (state->count == 0) {
			nullmask[idx] = true;
		} else {
			if (!Value::DoubleIsValid(state->value)) {
				throw OutOfRangeException("AVG is out of range!");
			}
			target[idx] = state->value / state->count;
		}
	}
};

AggregateFunction GetAverageAggregate(PhysicalType type) {
	switch (type) {
	case PhysicalType::INT16:
		return AggregateFunction::UnaryAggregate<avg_state_t<int64_t>, int16_t, double, IntegerAverageOperation>(
		    LogicalType::SMALLINT, LogicalType::DOUBLE);
	case PhysicalType::INT32: {
		auto function =
		    AggregateFunction::UnaryAggregate<avg_state_t<hugeint_t>, int32_t, double, IntegerAverageOperationHugeint>(
		        LogicalType::INTEGER, LogicalType::DOUBLE);
		return function;
	}
	case PhysicalType::INT64: {
		auto function =
		    AggregateFunction::UnaryAggregate<avg_state_t<hugeint_t>, int64_t, double, IntegerAverageOperationHugeint>(
		        LogicalType::BIGINT, LogicalType::DOUBLE);
		return function;
	}
	case PhysicalType::INT128:
		return AggregateFunction::UnaryAggregate<avg_state_t<hugeint_t>, hugeint_t, double, HugeintAverageOperation>(
		    LogicalType::HUGEINT, LogicalType::DOUBLE);
	default:
		throw NotImplementedException("Unimplemented average aggregate");
	}
}

unique_ptr<FunctionData> bind_decimal_avg(ClientContext &context, AggregateFunction &function,
                                          vector<unique_ptr<Expression>> &arguments) {
	auto decimal_type = arguments[0]->return_type;
	function = GetAverageAggregate(decimal_type.InternalType());
	function.name = "avg";
	function.arguments[0] = decimal_type;
	function.return_type = LogicalType::DOUBLE;
	return make_unique<AverageDecimalBindData>(Hugeint::Cast<double>(Hugeint::PowersOfTen[decimal_type.scale()]));
}

void AvgFun::RegisterFunction(BuiltinFunctions &set) {
	AggregateFunctionSet avg("avg");
	avg.AddFunction(AggregateFunction({LogicalType::DECIMAL}, LogicalType::DECIMAL, nullptr, nullptr, nullptr, nullptr,
	                                  nullptr, nullptr, bind_decimal_avg));
	avg.AddFunction(GetAverageAggregate(PhysicalType::INT16));
	avg.AddFunction(GetAverageAggregate(PhysicalType::INT32));
	avg.AddFunction(GetAverageAggregate(PhysicalType::INT64));
	avg.AddFunction(GetAverageAggregate(PhysicalType::INT128));
	avg.AddFunction(AggregateFunction::UnaryAggregate<avg_state_t<double>, double, double, NumericAverageOperation>(
	    LogicalType::DOUBLE, LogicalType::DOUBLE));
	set.AddFunction(avg);
}

} // namespace duckdb
