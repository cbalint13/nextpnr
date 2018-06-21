#!/bin/bash
set -ex
rm -f picorv32.v
wget https://raw.githubusercontent.com/cliffordwolf/picorv32/master/picorv32.v
yosys -p 'synth_ice40 -nocarry -blif picorv32.blif -top top' picorv32.v picorv32_top.v
arachne-pnr -d 8k --post-place-blif picorv32_place.blif picorv32.blif -o picorv32_arachne_all.asc
yosys -p "read_blif -wideports picorv32_place.blif; read_verilog -lib +/ice40/cells_sim.v; write_json picorv32_place.json"
./transform_arachne_loc.py picorv32_place.json > picorv32_place_nx.json
../nextpnr-ice40 --hx8k --asc picorv32_ar_placed.asc --json picorv32_place_nx.json --force