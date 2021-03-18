/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "basectx.h"

#include <boost/algorithm/string.hpp>

#include "context.h"
#include "design_utils.h"
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

const char *BaseCtx::nameOfBel(BelId bel) const
{
    const Context *ctx = getCtx();
    std::string &s = ctx->log_strs.next();
    ctx->getBelName(bel).build_str(ctx, s);
    return s.c_str();
}

const char *BaseCtx::nameOfWire(WireId wire) const
{
    const Context *ctx = getCtx();
    std::string &s = ctx->log_strs.next();
    ctx->getWireName(wire).build_str(ctx, s);
    return s.c_str();
}

const char *BaseCtx::nameOfPip(PipId pip) const
{
    const Context *ctx = getCtx();
    std::string &s = ctx->log_strs.next();
    ctx->getPipName(pip).build_str(ctx, s);
    return s.c_str();
}

const char *BaseCtx::nameOfGroup(GroupId group) const
{
    const Context *ctx = getCtx();
    std::string &s = ctx->log_strs.next();
    ctx->getGroupName(group).build_str(ctx, s);
    return s.c_str();
}

BelId BaseCtx::getBelByNameStr(const std::string &str)
{
    Context *ctx = getCtx();
    return ctx->getBelByName(IdStringList::parse(ctx, str));
}

WireId BaseCtx::getWireByNameStr(const std::string &str)
{
    Context *ctx = getCtx();
    return ctx->getWireByName(IdStringList::parse(ctx, str));
}

PipId BaseCtx::getPipByNameStr(const std::string &str)
{
    Context *ctx = getCtx();
    return ctx->getPipByName(IdStringList::parse(ctx, str));
}

GroupId BaseCtx::getGroupByNameStr(const std::string &str)
{
    Context *ctx = getCtx();
    return ctx->getGroupByName(IdStringList::parse(ctx, str));
}

void BaseCtx::addClock(IdString net, float freq)
{
    std::unique_ptr<ClockConstraint> cc(new ClockConstraint());
    cc->period = DelayPair(getCtx()->getDelayFromNS(1000 / freq));
    cc->high = DelayPair(getCtx()->getDelayFromNS(500 / freq));
    cc->low = DelayPair(getCtx()->getDelayFromNS(500 / freq));
    if (!net_aliases.count(net)) {
        log_warning("net '%s' does not exist in design, ignoring clock constraint\n", net.c_str(this));
    } else {
        getNetByAlias(net)->clkconstr = std::move(cc);
        log_info("constraining clock net '%s' to %.02f MHz\n", net.c_str(this), freq);
    }
}

void BaseCtx::createRectangularRegion(IdString name, int x0, int y0, int x1, int y1)
{
    std::unique_ptr<Region> new_region(new Region());
    new_region->name = name;
    new_region->constr_bels = true;
    new_region->constr_pips = false;
    new_region->constr_wires = false;
    for (int x = x0; x <= x1; x++) {
        for (int y = y0; y <= y1; y++) {
            for (auto bel : getCtx()->getBelsByTile(x, y))
                new_region->bels.insert(bel);
        }
    }
    region[name] = std::move(new_region);
}
void BaseCtx::addBelToRegion(IdString name, BelId bel) { region[name]->bels.insert(bel); }
void BaseCtx::constrainCellToRegion(IdString cell, IdString region_name)
{
    // Support hierarchical cells as well as leaf ones
    bool matched = false;
    if (hierarchy.count(cell)) {
        auto &hc = hierarchy.at(cell);
        for (auto &lc : hc.leaf_cells)
            constrainCellToRegion(lc.second, region_name);
        for (auto &hsc : hc.hier_cells)
            constrainCellToRegion(hsc.second, region_name);
        matched = true;
    }
    if (cells.count(cell)) {
        cells.at(cell)->region = region[region_name].get();
        matched = true;
    }
    if (!matched)
        log_warning("No cell matched '%s' when constraining to region '%s'\n", nameOf(cell), nameOf(region_name));
}
DecalXY BaseCtx::constructDecalXY(DecalId decal, float x, float y)
{
    DecalXY dxy;
    dxy.decal = decal;
    dxy.x = x;
    dxy.y = y;
    return dxy;
}

void BaseCtx::archInfoToAttributes()
{
    for (auto &cell : cells) {
        auto ci = cell.second.get();
        if (ci->bel != BelId()) {
            if (ci->attrs.find(id("BEL")) != ci->attrs.end()) {
                ci->attrs.erase(ci->attrs.find(id("BEL")));
            }
            ci->attrs[id("NEXTPNR_BEL")] = getCtx()->getBelName(ci->bel).str(getCtx());
            ci->attrs[id("BEL_STRENGTH")] = (int)ci->belStrength;
        }
        if (ci->constr_x != ci->UNCONSTR)
            ci->attrs[id("CONSTR_X")] = ci->constr_x;
        if (ci->constr_y != ci->UNCONSTR)
            ci->attrs[id("CONSTR_Y")] = ci->constr_y;
        if (ci->constr_z != ci->UNCONSTR) {
            ci->attrs[id("CONSTR_Z")] = ci->constr_z;
            ci->attrs[id("CONSTR_ABS_Z")] = ci->constr_abs_z ? 1 : 0;
        }
        if (ci->constr_parent != nullptr)
            ci->attrs[id("CONSTR_PARENT")] = ci->constr_parent->name.str(this);
        if (!ci->constr_children.empty()) {
            std::string constr = "";
            for (auto &item : ci->constr_children) {
                if (!constr.empty())
                    constr += std::string(";");
                constr += item->name.c_str(this);
            }
            ci->attrs[id("CONSTR_CHILDREN")] = constr;
        }
    }
    for (auto &net : getCtx()->nets) {
        auto ni = net.second.get();
        std::string routing;
        bool first = true;
        for (auto &item : ni->wires) {
            if (!first)
                routing += ";";
            routing += getCtx()->getWireName(item.first).str(getCtx());
            routing += ";";
            if (item.second.pip != PipId())
                routing += getCtx()->getPipName(item.second.pip).str(getCtx());
            routing += ";" + std::to_string(item.second.strength);
            first = false;
        }
        ni->attrs[id("ROUTING")] = routing;
    }
}

void BaseCtx::attributesToArchInfo()
{
    for (auto &cell : cells) {
        auto ci = cell.second.get();
        auto val = ci->attrs.find(id("NEXTPNR_BEL"));
        if (val != ci->attrs.end()) {
            auto str = ci->attrs.find(id("BEL_STRENGTH"));
            PlaceStrength strength = PlaceStrength::STRENGTH_USER;
            if (str != ci->attrs.end())
                strength = (PlaceStrength)str->second.as_int64();

            BelId b = getCtx()->getBelByNameStr(val->second.as_string());
            getCtx()->bindBel(b, ci, strength);
        }

        val = ci->attrs.find(id("CONSTR_PARENT"));
        if (val != ci->attrs.end()) {
            auto parent = cells.find(id(val->second.str));
            if (parent != cells.end())
                ci->constr_parent = parent->second.get();
            else
                continue;
        }

        val = ci->attrs.find(id("CONSTR_X"));
        if (val != ci->attrs.end())
            ci->constr_x = val->second.as_int64();

        val = ci->attrs.find(id("CONSTR_Y"));
        if (val != ci->attrs.end())
            ci->constr_y = val->second.as_int64();

        val = ci->attrs.find(id("CONSTR_Z"));
        if (val != ci->attrs.end())
            ci->constr_z = val->second.as_int64();

        val = ci->attrs.find(id("CONSTR_ABS_Z"));
        if (val != ci->attrs.end())
            ci->constr_abs_z = val->second.as_int64() == 1;

        val = ci->attrs.find(id("CONSTR_PARENT"));
        if (val != ci->attrs.end()) {
            auto parent = cells.find(id(val->second.as_string()));
            if (parent != cells.end())
                ci->constr_parent = parent->second.get();
        }
        val = ci->attrs.find(id("CONSTR_CHILDREN"));
        if (val != ci->attrs.end()) {
            std::vector<std::string> strs;
            auto children = val->second.as_string();
            boost::split(strs, children, boost::is_any_of(";"));
            for (auto val : strs) {
                if (cells.count(id(val.c_str())))
                    ci->constr_children.push_back(cells.find(id(val.c_str()))->second.get());
            }
        }
    }
    for (auto &net : getCtx()->nets) {
        auto ni = net.second.get();
        auto val = ni->attrs.find(id("ROUTING"));
        if (val != ni->attrs.end()) {
            std::vector<std::string> strs;
            auto routing = val->second.as_string();
            boost::split(strs, routing, boost::is_any_of(";"));
            for (size_t i = 0; i < strs.size() / 3; i++) {
                std::string wire = strs[i * 3];
                std::string pip = strs[i * 3 + 1];
                PlaceStrength strength = (PlaceStrength)std::stoi(strs[i * 3 + 2]);
                if (pip.empty())
                    getCtx()->bindWire(getCtx()->getWireByName(IdStringList::parse(getCtx(), wire)), ni, strength);
                else
                    getCtx()->bindPip(getCtx()->getPipByName(IdStringList::parse(getCtx(), pip)), ni, strength);
            }
        }
    }
    getCtx()->assignArchInfo();
}

NetInfo *BaseCtx::createNet(IdString name)
{
    NPNR_ASSERT(!nets.count(name));
    NPNR_ASSERT(!net_aliases.count(name));
    std::unique_ptr<NetInfo> net{new NetInfo};
    net->name = name;
    net_aliases[name] = name;
    NetInfo *ptr = net.get();
    nets[name] = std::move(net);
    refreshUi();
    return ptr;
}

void BaseCtx::connectPort(IdString net, IdString cell, IdString port)
{
    NetInfo *net_info = getNetByAlias(net);
    CellInfo *cell_info = cells.at(cell).get();
    connect_port(getCtx(), net_info, cell_info, port);
}

void BaseCtx::disconnectPort(IdString cell, IdString port)
{
    CellInfo *cell_info = cells.at(cell).get();
    disconnect_port(getCtx(), cell_info, port);
}

void BaseCtx::ripupNet(IdString name)
{
    NetInfo *net_info = getNetByAlias(name);
    std::vector<WireId> to_unbind;
    for (auto &wire : net_info->wires)
        to_unbind.push_back(wire.first);
    for (auto &unbind : to_unbind)
        getCtx()->unbindWire(unbind);
}
void BaseCtx::lockNetRouting(IdString name)
{
    NetInfo *net_info = getNetByAlias(name);
    for (auto &wire : net_info->wires)
        wire.second.strength = STRENGTH_USER;
}

CellInfo *BaseCtx::createCell(IdString name, IdString type)
{
    NPNR_ASSERT(!cells.count(name));
    std::unique_ptr<CellInfo> cell{new CellInfo};
    cell->name = name;
    cell->type = type;
    CellInfo *ptr = cell.get();
    cells[name] = std::move(cell);
    refreshUi();
    return ptr;
}

void BaseCtx::copyBelPorts(IdString cell, BelId bel)
{
    CellInfo *cell_info = cells.at(cell).get();
    for (auto pin : getCtx()->getBelPins(bel)) {
        cell_info->ports[pin].name = pin;
        cell_info->ports[pin].type = getCtx()->getBelPinType(bel, pin);
    }
}

NEXTPNR_NAMESPACE_END
