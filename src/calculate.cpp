#include "calculate.h"


namespace calculate {

    qSymbol Calculate::tokenize(const String &expr) const {
        qSymbol infix;

        auto next =
            std::sregex_iterator(
                expr.begin(), expr.end(), _regex
            ),
            end = std::sregex_iterator();

        while (next != end) {
            auto match = next->str();
            auto it = std::find(variables.begin(), variables.end(), match);
            if (it != variables.end()) {
                auto position = it - variables.begin();
                infix.push(symbols::newSymbol(_values.get() + position));
            }
            else {
                infix.push(symbols::newSymbol(match));
            }
            next++;
        }

        return infix;
    }

    qSymbol Calculate::check(qSymbol &&input) const {
        qSymbol output;
        pSymbol current, next;

        if (input.size() == 0)
            throw symbols::UndefinedSymbolException();

        current = input.front();
        input.pop();
        output.push(current);
        if (input.size() == 1 && !current->is(Type::CONSTANT))
            throw SyntaxErrorException();

        switch (current->type) {
            case (Type::RIGHT):
            case (Type::SEPARATOR):
            case (Type::OPERATOR):
                throw SyntaxErrorException();
            default:
                break;
        }

        while (!input.empty()) {
            next = input.front();
            input.pop();
            output.push(next);

            switch (current->type) {
                case (Type::CONSTANT):
                case (Type::RIGHT):
                    if (next->is(Type::RIGHT) ||
                        next->is(Type::SEPARATOR) ||
                        next->is(Type::OPERATOR))
                        break;
                case (Type::LEFT):
                case (Type::SEPARATOR):
                case (Type::OPERATOR):
                    if (next->is(Type::CONSTANT) ||
                        next->is(Type::LEFT) ||
                        next->is(Type::FUNCTION))
                        break;
                case (Type::FUNCTION):
                    if (next->is(Type::LEFT))
                        break;
                    throw SyntaxErrorException();
            }
            current = next;
        }

        switch (current->type) {
            case (Type::LEFT):
            case (Type::SEPARATOR):
            case (Type::OPERATOR):
            case (Type::FUNCTION):
                throw SyntaxErrorException();
            default:
                break;
        }

        return output;
    }

    qSymbol Calculate::shuntingYard(qSymbol &&infix) const {
        qSymbol postfix;
        sSymbol operations;

        while(!infix.empty()) {
            auto element = infix.front();
            infix.pop();

            switch (element->type) {
                case (Type::CONSTANT):
                    postfix.push(element);
                    break;

                case (Type::FUNCTION):
                    operations.push(element);
                    break;

                case (Type::SEPARATOR):
                    while (!operations.empty()) {
                        auto another = operations.top();
                        if (!another->is(Type::LEFT)) {
                            postfix.push(another);
                            operations.pop();
                        }
                        else {
                            break;
                        }
                    }
                    if (operations.empty())
                        throw ParenthesisMismatchException();
                    break;

                case (Type::OPERATOR):
                    while (!operations.empty()) {
                        auto another = operations.top();
                        if (another->is(Type::LEFT)) {
                            break;
                        }
                        else if (another->is(Type::FUNCTION)) {
                            postfix.push(another);
                            operations.pop();
                            break;
                        }
                        else {
                            auto op1 =
                                symbols::castChild<symbols::Operator>(element);
                            auto op2 =
                                symbols::castChild<symbols::Operator>(another);
                            if ((op1->left_assoc &&
                                 op1->precedence <= op2->precedence) ||
                                (!op1->left_assoc &&
                                 op1->precedence < op2->precedence)
                                ) {
                                operations.pop();
                                postfix.push(another);
                            }
                            else {
                                break;
                            }
                        }
                    }
                    operations.push(element);
                    break;

                case (Type::LEFT):
                    operations.push(element);
                    break;

                case (Type::RIGHT):
                    while (!operations.empty()) {
                        auto another = operations.top();
                        if (!another->is(Type::LEFT)) {
                            operations.pop();
                            postfix.push(another);
                        }
                        else {
                            break;
                        }
                    }
                    if (!operations.empty() &&
                        operations.top()->is(Type::LEFT)
                        )
                        operations.pop();
                    else
                        throw ParenthesisMismatchException();
                    break;

                default:
                    throw symbols::UndefinedSymbolException();
            }
        }

        while(!operations.empty()) {
            auto element = operations.top();
            if (element->is(Type::LEFT))
                throw ParenthesisMismatchException();
            operations.pop();
            postfix.push(element);
        }
        return postfix;
    }

    pSymbol Calculate::buildTree(qSymbol &&postfix) const {
        sSymbol operands;
        pSymbol element;

        while (!postfix.empty()) {
            element = postfix.front();
            postfix.pop();

            if (element->is(Type::CONSTANT)) {
                operands.push(element);
            }

            else if (element->is(Type::FUNCTION)) {
                auto function = symbols::castChild<symbols::Function>(element);
                auto args = function->args;
                vSymbol ops(args);
                for (auto i = args; i > 0; i--) {
                    if (operands.empty())
                        throw MissingArgumentsException();
                    ops[i - 1] = operands.top();
                    operands.pop();
                }
                function->addBranches(std::move(ops));
                operands.push(element);
            }

            else if (element->is(Type::OPERATOR)) {
                auto binary = symbols::castChild<symbols::Operator>(element);
                pSymbol a, b;
                if (operands.size() < 2)
                    throw MissingArgumentsException();
                b = operands.top();
                operands.pop();
                a = operands.top();
                operands.pop();
                binary->addBranches(a, b);
                operands.push(element);
            }

            else
                throw symbols::UndefinedSymbolException();
        }
        if (operands.size() > 1)
            throw ConstantsExcessException();

        return operands.top();
    }


    vString Calculate::extract(const String &vars) {
        auto nospaces = vars;
        nospaces.erase(
            std::remove_if(
                nospaces.begin(), nospaces.end(), [](char c) {return c == ' ';}
            ),
            nospaces.end()
        );

        auto stream = std::istringstream(nospaces);
        vString variables;

        String item;
        while(std::getline(stream, item, ','))
            variables.push_back(item);

        return variables;
    }

    vString Calculate::validate(const vString &vars) {
        static const auto regex = std::regex("[A-Za-z]+");
        vString variables;

        if (!std::all_of(vars.begin(), vars.end(),
            [](String var) {return std::regex_match(var, regex);})
            )
            throw BadNameException();

        auto noduplicates = vars;
        std::sort(noduplicates.begin(), noduplicates.end());
        noduplicates.erase(
            std::unique(noduplicates.begin(), noduplicates.end()),
            noduplicates.end()
        );

        if (noduplicates.size() != vars.size())
            throw DuplicateNameException();

        return vars;
    }


    Calculate::Calculate(const Calculate &other) :
        Calculate(other.expression, other.variables) {
    }

    Calculate::Calculate(Calculate &&other) :
        expression(other.expression), variables(other.variables),
        _values(new double[other.variables.size()]) {
        this->_regex = other._regex;
        this->_tree = std::move(other._tree);
    }

    Calculate::Calculate(const String &expr, const String &vars) :
        Calculate(expr, extract(vars)) {
    }

    Calculate::Calculate(const String &expr, const vString &vars) :
        expression(expr), variables(validate(vars)),
        _values(new double[vars.size()]) {

        if (expr.length() == 0)
            throw EmptyExpressionException();

        for (auto i = 0; i < vars.size(); i++)
            _values[i] = 0.;

        auto regex_string = "-?[0-9.]+|[A-Za-z]+|" +
            symbols::Operator::getSymbolsRegex();

        for (const String &var : vars)
            regex_string += "|" + var;
        _regex = std::regex(regex_string);

        auto infix = check(tokenize(expr));
        auto postfix = shuntingYard(std::move(infix));
        _tree = buildTree(std::move(postfix));
    }


    bool Calculate::operator==(const Calculate &other) const noexcept {
        return this->expression == other.expression;
    }

    double Calculate::operator() () const {
        return _tree->evaluate();
    };

    double Calculate::operator() (double value) const {
        if (variables.size() < 1)
            throw EvaluationException();
        _values[variables.size() - 1] = value;
        return _tree->evaluate();
    }

    double Calculate::operator() (vValue values) const {
        if (values.size() != variables.size())
            throw EvaluationException();
        for (auto i = 0; i < values.size(); i++)
            _values[i] = values[i];
        return _tree->evaluate();
    }

}


extern "C" {

    CALC_Expression CALC_newExpression(const char *expr, const char *vars) {
        using namespace calculate;

        try {
            CALC_Expression cexpr = static_cast<CALC_Expression>(
                new Calculate(expr, vars)
            );
            return cexpr;
        }
        catch (BaseSymbolException) {
            return nullptr;
        }
    }

    const char* CALC_getExpression(CALC_Expression cexpr) {
        using namespace calculate;

        if (!cexpr)
            return "";

        return static_cast<Calculate*>(cexpr)->expression.c_str();
    }

    int CALC_getVariables(CALC_Expression cexpr) {
        using namespace calculate;

        if (!cexpr)
            return -1;

        return static_cast<int>(
            static_cast<Calculate*>(cexpr)->variables.size()
        );
    }

    double CALC_evaluate(CALC_Expression cexpr, ...) {
        using namespace calculate;

        if (!cexpr)
            return std::numeric_limits<double>::quiet_NaN();

        vValue values;
        va_list list;
        va_start(list, cexpr);
        for (auto i = 0; i < CALC_getVariables(cexpr); i++)
            values.push_back(va_arg(list, double));
        va_end(list);

        try {
            return static_cast<Calculate*>(cexpr)->operator()(values);
        }
        catch (BaseSymbolException) {
            return std::numeric_limits<double>::quiet_NaN();
        }
    }

    double CALC_evalArray(CALC_Expression cexpr, double *v, unsigned s) {
        using namespace calculate;

        if (!cexpr)
            return std::numeric_limits<double>::quiet_NaN();

        vValue values(v, v + s);
        try {
            return static_cast<Calculate*>(cexpr)->operator()(values);
        }
        catch (BaseSymbolException) {
            return std::numeric_limits<double>::quiet_NaN();
        }
    }

    void CALC_freeExpression(CALC_Expression cexpr) {
        using namespace calculate;

        if (cexpr)
            delete static_cast<Calculate*>(cexpr);
    }
    
}
