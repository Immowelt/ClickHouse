#pragma once

#include <DB/Columns/ColumnArray.h>
#include <DB/DataTypes/DataTypeArray.h>
#include <DB/DataTypes/DataTypesNumberFixed.h>
#include <DB/AggregateFunctions/IAggregateFunction.h>

#include <common/logger_useful.h>


namespace DB
{


/** Не агрегатная функция, а адаптер агрегатных функций,
  *  который любую агрегатную функцию agg(x) делает агрегатной функцией вида aggArray(x).
  * Адаптированная агрегатная функция вычисляет вложенную агрегатную функцию для каждого элемента массива.
  */
class AggregateFunctionArray final : public IAggregateFunction
{
private:
	AggregateFunctionPtr nested_func_owner;
	IAggregateFunction * nested_func;
	size_t num_agruments;

public:
	AggregateFunctionArray(AggregateFunctionPtr nested_) : nested_func_owner(nested_), nested_func(nested_func_owner.get()) {}

	String getName() const override
	{
		return nested_func->getName() + "Array";
	}

	DataTypePtr getReturnType() const override
	{
		return nested_func->getReturnType();
	}

	void setArguments(const DataTypes & arguments) override
	{
		num_agruments = arguments.size();

		if (0 == num_agruments)
			throw Exception("Array aggregate functions requires at least one argument", ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

		DataTypes nested_arguments;
		for (size_t i = 0; i < num_agruments; ++i)
		{
			if (const DataTypeArray * array = typeid_cast<const DataTypeArray *>(&*arguments[i]))
				nested_arguments.push_back(array->getNestedType());
			else
				throw Exception("Illegal type " + arguments[i]->getName() + " of argument #" + toString(i + 1) + " for aggregate function " + getName() + ". Must be array.", ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);
		}

		nested_func->setArguments(nested_arguments);
	}

	void setParameters(const Array & params) override
	{
		nested_func->setParameters(params);
	}

	void create(AggregateDataPtr place) const override
	{
		nested_func->create(place);
	}

	void destroy(AggregateDataPtr place) const noexcept override
	{
		nested_func->destroy(place);
	}

	bool hasTrivialDestructor() const override
	{
		return nested_func->hasTrivialDestructor();
	}

	size_t sizeOfData() const override
	{
		return nested_func->sizeOfData();
	}

	size_t alignOfData() const override
	{
		return nested_func->alignOfData();
	}

	void add(AggregateDataPtr place, const IColumn ** columns, size_t row_num, Arena * arena) const override
	{
		const IColumn * nested[num_agruments];

		for (size_t i = 0; i < num_agruments; ++i)
			nested[i] = &static_cast<const ColumnArray &>(*columns[i]).getData();

		const ColumnArray & first_array_column = static_cast<const ColumnArray &>(*columns[0]);
		const IColumn::Offsets_t & offsets = first_array_column.getOffsets();

		size_t begin = row_num == 0 ? 0 : offsets[row_num - 1];
		size_t end = offsets[row_num];

		for (size_t i = begin; i < end; ++i)
			nested_func->add(place, nested, i, arena);
	}

	void merge(AggregateDataPtr place, ConstAggregateDataPtr rhs, Arena * arena) const override
	{
		nested_func->merge(place, rhs, arena);
	}

	void serialize(ConstAggregateDataPtr place, WriteBuffer & buf) const override
	{
		nested_func->serialize(place, buf);
	}

	void deserialize(AggregateDataPtr place, ReadBuffer & buf, Arena * arena) const override
	{
		nested_func->deserialize(place, buf, arena);
	}

	void insertResultInto(ConstAggregateDataPtr place, IColumn & to) const override
	{
		nested_func->insertResultInto(place, to);
	}

	bool allocatesMemoryInArena() const override
	{
		return nested_func->allocatesMemoryInArena();
	}

	static void addFree(const IAggregateFunction * that, AggregateDataPtr place, const IColumn ** columns, size_t row_num, Arena * arena)
	{
		static_cast<const AggregateFunctionArray &>(*that).add(place, columns, row_num, arena);
	}

	IAggregateFunction::AddFunc getAddressOfAddFunction() const override final { return &addFree; }
};




////////////////////////////////7




class AggregateFunctionForEach final : public IAggregateFunction
{
private:
	AggregateFunctionPtr nested_func_owner;
	IAggregateFunction * nested_func;
	UInt64 array_size = 3;


public:
	AggregateFunctionForEach(AggregateFunctionPtr nested_) : nested_func_owner(nested_), nested_func(nested_func_owner.get()) {}

	String getName() const override
	{
		return nested_func->getName() + "ForEach";
	}

	DataTypePtr getReturnType() const override
	{
		LOG_TRACE(&Logger::root(), "getreturntype");
		return std::make_shared<DataTypeArray>(nested_func->getReturnType());
	}

	void create(AggregateDataPtr place) const override
	{
		LOG_TRACE(&Logger::root(), "create");
		for(UInt64 i = 0; i < array_size; ++i)
			nested_func->create(place + i * nested_func->sizeOfData());
	}

	void destroy(AggregateDataPtr place) const noexcept override
	{
		LOG_TRACE(&Logger::root(), "destroy");
		for(UInt64 i = 0; i < array_size; ++i)
			nested_func->destroy(place + i * nested_func->sizeOfData());
	}

	bool hasTrivialDestructor() const override
	{
		return nested_func->hasTrivialDestructor();
	}

	size_t sizeOfData() const override
	{
		LOG_TRACE(&Logger::root(), "siuzeofdata");
		return array_size * nested_func->sizeOfData();
	}

	size_t alignOfData() const override
	{
		return nested_func->alignOfData();
	}


	void setArguments(const DataTypes & arguments) override
	{
		LOG_TRACE(&Logger::root(), "setarg");
		size_t num_arguments = arguments.size();

		if (1 != num_arguments)
			throw Exception("Aggregate functions of the xxxEach group require exactly one argument of array type", ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

		DataTypes nested_argument;
		if (const DataTypeArray * array = typeid_cast<const DataTypeArray *>(&*arguments[0]))
			nested_argument.push_back(array->getNestedType());
		else
			throw Exception("Illegal type " + arguments[0]->getName() + " of first argument for aggregate function " + getName() + ". Must be array.", ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

		nested_func->setArguments(nested_argument);
	}

	void setParameters(const Array & params) override
	{
		LOG_TRACE(&Logger::root(), "setparams");
		if (1 != params.size())
			throw Exception("Aggregate functions of the xxxEach group require resulting array size as parameter ", ErrorCodes::PARAMETER_OUT_OF_BOUND);

		if (params[0].getType() != DB::Field::Types::UInt64)
			throw Exception("Aggregate functions of the xxxEach group require one positive parameter of type int", ErrorCodes::PARAMETER_OUT_OF_BOUND);

		array_size = params[0].get<UInt64>();

		if (0 == array_size)
			throw Exception("Aggregate functions of the xxxEach group require one positive parameter of type int", ErrorCodes::PARAMETER_OUT_OF_BOUND);
	}

	void add(AggregateDataPtr place, const IColumn ** columns, size_t row_num, Arena * arena) const override
	{
		//data(place).value.push_back(Array::value_type());
		//column.get(row_num, data(place).value.back());
		LOG_TRACE(&Logger::root(), "add row "+std::to_string(row_num) );

		const ColumnArray & first_array_column = static_cast<const ColumnArray &>(*columns[0]);
		const IColumn::Offsets_t & offsets = first_array_column.getOffsets();
		const IColumn* array_data = &first_array_column.getData();

		size_t begin = row_num == 0 ? 0 : offsets[row_num - 1];
		size_t end = offsets[row_num];

		if (end > begin + array_size)
			end = begin + array_size;

		for(size_t i = begin; i < end; ++i)
		{
			auto z = first_array_column.getDataPtr();
			Field new_value;
			z->get(i, new_value);

			LOG_TRACE(&Logger::root(), "add "+std::to_string(i) + " " + std::to_string(size_t(place + (i - begin) * nested_func->sizeOfData())) + " " + std::to_string(new_value.get<Int8>())) ;
			nested_func->add(place + (i - begin) * nested_func->sizeOfData(), static_cast<const IColumn**>(&array_data), i, arena);
		}
	}

	void merge(AggregateDataPtr place, ConstAggregateDataPtr rhs, Arena * arena) const override
	{
		for(UInt64 i = 0; i < array_size; ++i)
			nested_func->merge(place + i * nested_func->sizeOfData(), rhs + i * nested_func->sizeOfData(), arena);
	}

	void serialize(ConstAggregateDataPtr place, WriteBuffer & buf) const override
	{
		for(UInt64 i = 0; i < array_size; ++i)
			nested_func->serialize(place + i * nested_func->sizeOfData(), buf);
	}

	void deserialize(AggregateDataPtr place, ReadBuffer & buf, Arena * arena) const override
	{
		for(UInt64 i = 0; i < array_size; ++i)
			nested_func->deserialize(place + i * nested_func->sizeOfData(), buf, arena);
	}

	void insertResultInto(ConstAggregateDataPtr place, IColumn & to) const override
	{
		//Insert results of nested functions into array_size temporary columns
		Columns temporaryColumns;
		size_t temporaryColumnSize = 0;
		for(UInt64 i = 0; i < array_size; ++i)
		{
			ColumnPtr temporaryColumn = nested_func->getReturnType()->createColumn();
			temporaryColumns.push_back(temporaryColumn);

			nested_func->insertResultInto(place + i * nested_func->sizeOfData(), *temporaryColumn);

			if (temporaryColumnSize == 0)
				temporaryColumnSize = temporaryColumn->size();
			else
				if (temporaryColumnSize != temporaryColumn->size())
					throw Exception("AggregateFunctionEach::insertResultInto: Column sizes don't match", ErrorCodes::PARAMETER_OUT_OF_BOUND);
		}

		// Convert array_size columns of temporaryColumnSize each into one array column of temporaryColumnSize
		ColumnArray & arr_to = static_cast<ColumnArray &>(to);
		ColumnArray::Offsets_t & offsets_to = arr_to.getOffsets();
		ColumnPtr arr_data = arr_to.getDataPtr();

		for(size_t row = 0; row < temporaryColumnSize; ++row)
		{
			offsets_to.push_back((offsets_to.size() == 0 ? 0 : offsets_to.back()) + array_size);
			for(UInt64 i = 0; i < array_size; ++i)
				arr_data->insertFrom(*temporaryColumns[i], row);
		}
	}

	bool allocatesMemoryInArena() const override
	{
		return nested_func->allocatesMemoryInArena();
	}

	static void addFree(const IAggregateFunction * that, AggregateDataPtr place, const IColumn ** columns, size_t row_num, Arena * arena)
	{
		static_cast<const AggregateFunctionForEach &>(*that).add(place, columns, row_num, arena);
	}

	IAggregateFunction::AddFunc getAddressOfAddFunction() const override final { return &addFree; }
};














}
