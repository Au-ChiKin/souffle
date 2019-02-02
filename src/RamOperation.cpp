/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2013, 2014, Oracle and/or its affiliates. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file RamOperation.cpp
 *
 * Implements the operation of a relational algebra query consisting of
 * Search/Scan and a Project operation. The Search/Scan operation traverses
 * a table and/or check for condition of each tuple and/or uses an index.
 *
 ***********************************************************************/

#include "RamOperation.h"
#include "BinaryConstraintOps.h"
#include "RamCondition.h"
#include "RamRelation.h"
#include "RamVisitor.h"
#include "RamValue.h"
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>

namespace souffle {

namespace {


/** Determine whether a RAM value is a constant */
bool isConstant(const RamValue* value) {
    // visitor 
    class ConstValueVisitor : public RamVisitor<bool> {
    public:
        // number
        bool visitNumber(const RamNumber& num) override {
            return true;
        }

        // tuple element access
        bool visitElementAccess(const RamElementAccess& elem) override {
            return false;
        }

        // auto increment
        bool visitAutoIncrement(const RamAutoIncrement& increment) override {
            return false;
        }

        // intrinsic functors
        bool visitIntrinsicOperator(const RamIntrinsicOperator& op) override {
            const auto& args = op.getArguments();
            bool isConst = true;
            for (const auto arg : args) {
                isConst = isConst && visit(arg);
            }
            return isConst;
        }

        // pack operator
        bool visitPack(const RamPack& pack) override {
            return false;
        }

        // argument
        bool visitArgument(const RamArgument& arg) override {
            return false;
        }

        // user defined operator
        bool visitUserDefinedOperator(const RamUserDefinedOperator& op) override {
            const auto& args = op.getArguments();
            bool isConst = true;
            for (const auto arg : args) {
                isConst = isConst && visit(arg);
            }
            return isConst;
        }
    };
    return ConstValueVisitor().visit(value);
}

/** get indexable element */
std::unique_ptr<RamValue> getIndexElement(RamCondition* c, size_t& element, size_t level) {
    if (auto* binRelOp = dynamic_cast<RamBinaryRelation*>(c)) {
        if (binRelOp->getOperator() == BinaryConstraintOp::EQ) {
            if (auto* lhs = dynamic_cast<RamElementAccess*>(binRelOp->getLHS())) {
                RamValue* rhs = binRelOp->getRHS();
                if (lhs->getLevel() == level && (isConstant(rhs) || rhs->getLevel() < level)) {
                    element = lhs->getElement();
                    return binRelOp->takeRHS();
                }
            }
            if (auto* rhs = dynamic_cast<RamElementAccess*>(binRelOp->getRHS())) {
                RamValue* lhs = binRelOp->getLHS();
                if (rhs->getLevel() == level && (isConstant(lhs) || lhs->getLevel() < level)) {
                    element = rhs->getElement();
                    return binRelOp->takeLHS();
                }
            }
        }
    }
    return std::unique_ptr<RamValue>(nullptr);
}

}  // namespace

/** add condition */
void RamAggregate::addCondition(std::unique_ptr<RamCondition> newCondition) {
    assert(newCondition->getLevel() == getIdentifier());

    // use condition to narrow scan if possible
    size_t element = 0;
    if (std::unique_ptr<RamValue> value = getIndexElement(newCondition.get(), element, getIdentifier())) {
        if (element > 0 || relation->getName().find("__agg") == std::string::npos) {
            keys |= (1 << element);
            if (pattern[element] == nullptr) {
                pattern[element] = std::move(value);
            } else {
                std::unique_ptr<RamValue> field(new RamElementAccess(getIdentifier(), element));

                auto addCondition = [&](std::unique_ptr<RamCondition> c) {
                    assert(c->getLevel() == getIdentifier());
                    if (condition != nullptr) {
                        condition = std::make_unique<RamAnd>(std::move(condition), std::move(c));
                    } else {
                        condition = std::move(c);
                    }
                };

                addCondition(std::make_unique<RamBinaryRelation>(
                        BinaryConstraintOp::EQ, std::move(field), std::move(value)));
            }
        } else {
            std::unique_ptr<RamValue> field(new RamElementAccess(getIdentifier(), element));
            std::unique_ptr<RamCondition> eq(
                    new RamBinaryRelation(BinaryConstraintOp::EQ, std::move(field), std::move(value)));
            if (condition != nullptr) {
                condition = std::make_unique<RamAnd>(std::move(condition), std::move(eq));
            } else {
                condition.swap(eq);
            }
        }
    }
}

}  // end of namespace souffle
