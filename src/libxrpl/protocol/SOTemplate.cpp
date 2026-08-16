//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpl/protocol/SOTemplate.h>
#include <vector>

namespace ripple {

namespace {

void
finishTemplate(std::vector<SOElement>& elements, std::vector<int>& indices)
{
    for (std::size_t i = 0; i < elements.size(); ++i)
    {
        SField const& sField{elements[i].sField()};

        if (sField.getNum() <= 0 || sField.getNum() >= indices.size())
            Throw<std::runtime_error>("Invalid field index for SOTemplate.");

        if (indices[sField.getNum()] != -1)
            Throw<std::runtime_error>(
                std::string("Duplicate field index for SOTemplate. ") +
                sField.fieldName);

        indices[sField.getNum()] = static_cast<int>(i);
    }
}

}  // namespace

SOTemplate::SOTemplate(
    std::initializer_list<SOElement> uniqueFields,
    std::initializer_list<SOElement> commonFields)
    : indices_(SField::getNumFields() + 1, -1)  // Unmapped indices == -1
{
    elements_.reserve(uniqueFields.size() + commonFields.size());
    elements_.assign(uniqueFields);
    elements_.insert(elements_.end(), commonFields);
    finishTemplate(elements_, indices_);
}

SOTemplate::SOTemplate(
    std::initializer_list<SOElement> uniqueFields,
    std::vector<SOElement> const& commonFields)
    : indices_(SField::getNumFields() + 1, -1)
{
    elements_.reserve(uniqueFields.size() + commonFields.size());
    elements_.assign(uniqueFields);
    elements_.insert(elements_.end(), commonFields.begin(), commonFields.end());
    finishTemplate(elements_, indices_);
}

int
SOTemplate::getIndex(SField const& sField) const
{
    // The mapping table should be large enough for any possible field
    //
    if (sField.getNum() <= 0 || sField.getNum() >= indices_.size())
        Throw<std::runtime_error>("Invalid field index for getIndex().");

    return indices_[sField.getNum()];
}

}  // namespace ripple
