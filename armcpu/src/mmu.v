/*
 * $File: mmu.v
 * $Date: Fri Dec 13 17:18:14 2013 +0800
 * $Author: jiakai <jia.kai66@gmail.com>
 */

`include "mem_opt.vh"
`include "common.vh"

// memory management unit
module mmu(
	input clk,
	input rst,

	input [`TLB_WRITE_STRUCT_WIDTH-1:0] tlb_write_struct,

	input [31:0] instr_addr,
	output [31:0] instr_out,

	// data_in would be latched on posedge on data_opt is write,
	// but data_addr would not
	// data_opt should only be set for one cycle
	input [`MEM_OPT_WIDTH-1:0] data_opt,
	input [31:0] data_addr,
	input [31:0] data_in,
	// data_out is valid for one cycle after busy is deasserted
	output reg [31:0] data_out,

	// busy would be asserted immediately after data_opt != NONE,
	// before next clock edge
	output busy,

	// first check exception; if it happens, no actual operation would be
	// performed and busy is not set
	output reg [`EXC_CODE_WIDTH-1:0] exc_code,
	
	// interface to physical memory controller
	// it is required to response before next posedge
	// and also latch both data and addr, if multiple-cycle for one operation
	// needed
	output [31:0] dev_mem_addr,
	input [31:0] dev_mem_data_in,
	output reg [31:0] dev_mem_data_out,
	output dev_mem_is_write,
	output opt_is_lw,
	input dev_mem_busy);

	// ------------------------------------------------------------------

	always @(*) begin
		if (instr_addr[1:0])
			;//$warning("time=%g unaligned instr_addr: %h", $time, instr_addr);
	end

	wire tlb_write_enable;
	wire [`TLB_INDEX_WIDTH-1:0] tlb_write_index;
	wire [`TLB_ENTRY_WIDTH-1:0] tlb_write_entry;
	wire tlb_missing, tlb_writable;
	reg [`TLB_ENTRY_WIDTH-1:0] tlb_mem[0:`TLB_NR_ENTRY-1];

	assign {tlb_write_enable, tlb_write_index, tlb_write_entry} = tlb_write_struct;


	assign mem_opt_is_write = `MEM_OPT_IS_WRITE(data_opt);
	assign instr_out = dev_mem_data_in;

	localparam READ = 2'b00, WRITE_SB = 2'b01, WRITE_DO_WRITE = 2'b10;
	reg [1:0] state;

	// if multiple-cycle read is needed, dev_mem_busy should be set before
	// next posedge, so busy is also set
	assign busy =
		(state != READ || dev_mem_busy || mem_opt_is_write) &&
		exc_code == `EC_NONE;


	wire [31:0]
		mem_unaligned_addr =
			state == READ && data_opt == `MEM_OPT_NONE ? instr_addr : data_addr,
		mem_vrt_addr = {mem_unaligned_addr[31:2], 2'b00};


	assign dev_mem_is_write = state == WRITE_DO_WRITE;
	assign opt_is_lw = data_opt == `MEM_OPT_LW;


	// select a byte in input word according to addr
	reg [7:0] dev_mem_data_in_selected_byte;
	always @(*)
		case (mem_unaligned_addr[1:0])
			0: dev_mem_data_in_selected_byte = dev_mem_data_in[7:0];
			1: dev_mem_data_in_selected_byte = dev_mem_data_in[15:8];
			2: dev_mem_data_in_selected_byte = dev_mem_data_in[23:16];
			3: dev_mem_data_in_selected_byte = dev_mem_data_in[31:24];
		endcase
	wire [15:0] dev_mem_data_in_selected_word = mem_unaligned_addr[1] ?
		dev_mem_data_in[31:16] : dev_mem_data_in[15:0];


	reg [1:0] mem_align_require;
	always @(*) begin
		mem_align_require <= 2'b00;
		case (data_opt)
			`MEM_OPT_LW, `MEM_OPT_SW:
				mem_align_require <= 2'b11;
			`MEM_OPT_LHU:
				mem_align_require <= 2'b01;
			`MEM_OPT_NONE:
				if (state == READ)
					mem_align_require <= 2'b11;
		endcase
	end


	// handle exception
	always @(*) begin
		// TODO: check user/kernel permission
		exc_code = `EC_NONE;
		if (mem_unaligned_addr[1:0] & mem_align_require) begin
			if (mem_opt_is_write) begin
				exc_code = `EC_ADES;
				/*
				$warning("time=%g unaligned mem_addr write word: %h", $time,
					mem_unaligned_addr);
				*/
			end else begin
				exc_code = `EC_ADEL;
				/*
				$warning("time=%g unaligned mem_addr read, data_opt=%h: %h", $time,
					data_opt, mem_unaligned_addr);
				*/
			end
		end
		if (tlb_missing) begin
			if (mem_opt_is_write) begin
				exc_code = `EC_TLBS;
				/*
				$warning("time=%g TLB write missing, vaddr=%h",
					$time, mem_vrt_addr);
				*/
			end else begin
				exc_code = `EC_TLBL;
				/*
				$warning("time=%g TLB read missing, vaddr=%h",
					$time, mem_vrt_addr);
				*/
			end
		end else if (!tlb_writable && mem_opt_is_write) begin
			exc_code = `EC_TLB_MOD;
			/*
			$warning("time=%g write to read-only page, vaddr=%h",
				$time, mem_vrt_addr);
			*/
		end
	end

	// --------------------------- tlb lookup ------------------------------
	// TODO: user/kernel permission check

	// cache unimplemented
	assign impl_tlb_direct_mapping =
		(mem_vrt_addr[31:28] >= 4'h8 && mem_vrt_addr[31:28] <= 4'hb);

	wire [0:`TLB_NR_ENTRY*2-1] impl_tlb_hit_entry;
	wire [0:`TLB_NR_ENTRY-1] impl_tlb_addr_equal;
	wire [0:`TLB_NR_ENTRY*2-1] impl_tlb_hit_entry_comb [20:0];
	wire [20:0] impl_tlb_lookup_result;
	genvar i, j;
	generate
		for (i = 0; i < `TLB_NR_ENTRY; i = i + 1)  begin
			assign impl_tlb_addr_equal[i] =
				tlb_mem[i][62:44] == mem_vrt_addr[31:13];
			assign impl_tlb_hit_entry[i * 2] =
				impl_tlb_addr_equal[i] & tlb_mem[i][0] & ~mem_vrt_addr[12];
			assign impl_tlb_hit_entry[i * 2 + 1] =
				impl_tlb_addr_equal[i] & tlb_mem[i][22] & mem_vrt_addr[12];

			for (j = 0; j <= 20; j = j + 1) begin
				assign impl_tlb_hit_entry_comb[j][i * 2]
					= impl_tlb_hit_entry[i * 2] & tlb_mem[i][j + 1];
				assign impl_tlb_hit_entry_comb[j][i * 2 + 1]
					= impl_tlb_hit_entry[i * 2 + 1] & tlb_mem[i][j + 23];
			end
		end

		for (i = 0; i <= 20; i = i + 1)
			assign impl_tlb_lookup_result[i] = |impl_tlb_hit_entry_comb[i];
	endgenerate

	assign tlb_missing = ~|{impl_tlb_direct_mapping, impl_tlb_hit_entry};
	assign tlb_writable = impl_tlb_direct_mapping | impl_tlb_lookup_result[0];
	assign dev_mem_addr = impl_tlb_direct_mapping ?
		{3'b0, mem_vrt_addr[28:0]} : {impl_tlb_lookup_result[20:1], mem_vrt_addr[11:0]};

	// -------------------------- end tlb lookup ----------------------------

	// assign data output
	always @(*)
		case (data_opt)
			`MEM_OPT_LW:
				data_out = dev_mem_data_in;
			`MEM_OPT_LBS:
				data_out = {{24{dev_mem_data_in_selected_byte[7]}}, dev_mem_data_in_selected_byte};
			`MEM_OPT_LBU:
				data_out = {{24'b0}, dev_mem_data_in_selected_byte};
			`MEM_OPT_LHU:
				data_out = {{16'b0}, dev_mem_data_in_selected_word};
			default:
				data_out = 0;
		endcase


	// main fsm
	always @(posedge clk)
		if (rst)
			state <= READ;
		else case (state)
			READ: if (!dev_mem_busy &&
					data_opt != `MEM_OPT_NONE && exc_code == `EC_NONE) begin
				dev_mem_data_out <= data_in;
				if (data_opt == `MEM_OPT_SB)
					state <= WRITE_SB;
				else if (data_opt == `MEM_OPT_SW)
					state <= WRITE_DO_WRITE;
			end
			WRITE_SB: begin: WRITE_BYTE_INIT
				integer i, j;
				for (i = 0; i < 4; i = i + 1)
					if (mem_unaligned_addr[1:0] != i) begin
						for (j = i * 8; j < (i + 1) * 8; j = j + 1)
							dev_mem_data_out[j] <= dev_mem_data_in[j];
					end else begin
						for (j = 0; j < 8; j = j + 1)
							dev_mem_data_out[j + i * 8] <= dev_mem_data_out[j];
					end
				state <= WRITE_DO_WRITE;
			end
			WRITE_DO_WRITE:
				state <= READ;
			default:
				state <= READ;
		endcase


	// handle tlb write
	always @(posedge clk)
		if (rst) begin: INIT_TLB
			integer i;
			for (i = 0; i < `TLB_NR_ENTRY; i = i + 1)
				tlb_mem[i] <= 0;
		end else if (tlb_write_enable) begin
			tlb_mem[tlb_write_index] <= tlb_write_entry;
			$display("time=%g write TLB: index=%h VPN2=%h P1=(%h, %b) P0=(%h, %b)",
				$time, tlb_write_index, tlb_write_entry[62:44],
				tlb_write_entry[43:24], tlb_write_entry[23:22],
				tlb_write_entry[21:2], tlb_write_entry[1:0]);
		end

endmodule

