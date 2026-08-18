#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Arm2x86 GDB Plugin - Debug ARM to x86_64 Dynamic Binary Translation
Provides commands for inspecting translation cache, performance stats, and execution flow.

Usage in GDB:
    source gdb_arm2x86.py
    arm2x86 info cache
    arm2x86 stats
    arm2x86 trace on
    arm2x86 dump PC 0x12345678
"""

import gdb
import sys

class Arm2x86Command(gdb.Command):
    """Arm2x86 DBT debugging commands"""
    
    def __init__(self):
        super(Arm2x86Command, self).__init__("arm2x86", gdb.COMMAND_USER,
                                           gdb.COMPLETE_COMMAND, True)
    
    def invoke(self, arg, from_tty):
        args = gdb.string_to_argv(arg)
        if not args:
            self.help()
            return
        
        cmd = args[0].lower()
        
        if cmd == "info":
            self.cmd_info(args[1:] if len(args) > 1 else [])
        elif cmd == "stats":
            self.cmd_stats()
        elif cmd == "trace":
            self.cmd_trace(args[1:] if len(args) > 1 else [])
        elif cmd == "dump":
            self.cmd_dump(args[1:] if len(args) > 1 else [])
        elif cmd == "cache":
            self.cmd_cache()
        elif cmd == "help":
            self.help()
        else:
            print("Unknown command: " + cmd)
            self.help()
    
    def help(self):
        print("Arm2x86 GDB Plugin Commands:")
        print("  arm2x86 info [cache|regs|config]  - Show information")
        print("  arm2x86 stats                     - Show performance statistics")
        print("  arm2x86 trace [on|off|clear]      - Control execution trace")
        print("  arm2x86 dump <address>            - Dump translation entry")
        print("  arm2x86 cache                     - Show cache status")
        print("  arm2x86 help                      - Show this help")
    
    def cmd_info(self, args):
        """Show Arm2x86 information"""
        if not args or args[0] == "cache":
            self.cmd_cache()
        elif args[0] == "regs":
            self.show_regs()
        elif args[0] == "config":
            self.show_config()
        else:
            print("Unknown info type: " + args[0])
    
    def cmd_stats(self):
        """Show performance statistics"""
        print("=" * 60)
        print("Arm2x86 Performance Statistics")
        print("=" * 60)
        
        try:
            # Try to read global perf stats
            perf_stats = gdb.parse_and_eval("g_arm2x86_perf")
            print("Total translations:  %d" % int(perf_stats['total_translations']))
            print("Cache hits:          %d" % int(perf_stats['cache_hits']))
            print("Cache misses:        %d" % int(perf_stats['cache_misses']))
            print("Translation time:    %d ms" % int(perf_stats['translation_time_ns'] / 1000000))
            print("Execution time:      %d ms" % int(perf_stats['execution_time_ns'] / 1000000))
            
            hit_rate = float(perf_stats['cache_hits']) / (perf_stats['cache_hits'] + perf_stats['cache_misses']) * 100
            print("Cache hit rate:      %.2f%%" % hit_rate)
        except Exception as e:
            print("Failed to read stats: %s" % str(e))
            
        print("=" * 60)
    
    def cmd_trace(self, args):
        """Control execution trace"""
        if not args:
            trace_enabled = gdb.parse_and_eval("g_arm2x86_trace_enabled")
            print("Trace is: %s" % ("ON" if trace_enabled else "OFF"))
            return
        
        action = args[0].lower()
        if action == "on":
            gdb.execute("set g_arm2x86_trace_enabled = 1")
            print("Trace enabled")
        elif action == "off":
            gdb.execute("set g_arm2x86_trace_enabled = 0")
            print("Trace disabled")
        elif action == "clear":
            gdb.execute("set g_arm2x86_trace_count = 0")
            print("Trace cleared")
        else:
            print("Unknown trace action: " + action)
    
    def cmd_dump(self, args):
        """Dump translation entry for an address"""
        if not args:
            print("Usage: arm2x86 dump <address>")
            return
        
        try:
            addr = parse_address(args[0])
            print("=" * 60)
            print("Translation Entry for 0x%x" % addr)
            print("=" * 60)
            
            # Try to lookup in cache
            cache = gdb.parse_and_eval("g_arm2x86_tcache")
            entry = cache_lookup(cache, addr)
            
            if entry:
                print("ARM Address:    0x%x" % int(entry['arm_addr']))
                print("x86 Code:       0x%x" % int(entry['x86_code']))
                print("x86 Size:       %d bytes" % int(entry['x86_size']))
                print("Exec Count:     %d" % int(entry['exec_count']))
                print("Flags:         0x%x" % int(entry['flags']))
                
                # Disassemble x86 code
                code = int(entry['x86_code'])
                size = int(entry['x86_size'])
                print("\nx86 Disassembly:")
                disasm_code(code, min(size, 64))
            else:
                print("No translation found for 0x%x" % addr)
                
            print("=" * 60)
        except Exception as e:
            print("Error: %s" % str(e))
    
    def cmd_cache(self):
        """Show translation cache status"""
        print("=" * 60)
        print("Arm2x86 Translation Cache")
        print("=" * 60)
        
        try:
            cache = gdb.parse_and_eval("g_arm2x86_tcache")
            
            print("Total size:     %d bytes" % int(cache['total_size']))
            print("Used size:      %d bytes" % int(cache['used_size']))
            print("Entry count:    %d" % int(cache['entry_count']))
            print("Lookup count:   %d" % int(cache['lookup_count']))
            print("Miss count:     %d" % int(cache['miss_count']))
            
            usage = float(cache['used_size']) / cache['total_size'] * 100
            print("Cache usage:    %.2f%%" % usage)
            
            miss_rate = float(cache['miss_count']) / cache['lookup_count'] * 100 if cache['lookup_count'] > 0 else 0
            print("Miss rate:      %.2f%%" % miss_rate)
            
        except Exception as e:
            print("Failed to read cache: %s" % str(e))
            
        print("=" * 60)
    
    def show_regs(self):
        """Show ARM/x86 register mapping"""
        print("=" * 60)
        print("Register Mapping")
        print("=" * 60)
        print("ARM64          ->  x86_64")
        print("-" * 60)
        print("X0-R0         ->  RAX")
        print("X1-R1         ->  RCX")
        print("X2-R2         ->  RDX")
        print("X3-R3         ->  RBX")
        print("X4-R4         ->  RSP (saved)")
        print("X5-R5         ->  RSI")
        print("X6-R6         ->  RDI")
        print("X7-R7         ->  R8")
        print("X8-R8         ->  R9")
        print("X9-R9         ->  R10")
        print("X10-R10       ->  R11")
        print("X11-R11       ->  R12")
        print("X12-R12       ->  R13")
        print("X13-R13       ->  R14")
        print("X14-R14       ->  R15")
        print("X15-R15       ->  [spill]")
        print("X16-R16       ->  [spill]")
        print("X29-FP        ->  RBP")
        print("X30-LR        ->  [link register]")
        print("X31-SP        ->  RSP")
        print("=" * 60)
    
    def show_config(self):
        """Show Arm2x86 configuration"""
        print("=" * 60)
        print("Arm2x86 Configuration")
        print("=" * 60)
        
        try:
            simd = gdb.parse_and_eval("g_simd_enabled")
            print("SIMD enabled:   %s" % ("YES" if simd else "NO"))
            
            # Try to read debug flags
            debug_flags = gdb.parse_and_eval("g_arm2x86_debug_flags")
            print("Debug flags:    0x%x" % int(debug_flags))
            
            # Read hot threshold
            threshold = gdb.parse_and_eval("ARM2X86_HOT_THRESHOLD")
            print("Hot threshold:  %d" % int(threshold))
            
        except Exception as e:
            print("Failed to read config: %s" % str(e))
            
        print("=" * 60)


def parse_address(arg):
    """Parse address from string argument"""
    try:
        if arg.startswith("0x"):
            return int(arg, 16)
        else:
            return int(arg)
    except ValueError:
        return int(gdb.parse_and_eval(arg))


def cache_lookup(cache, addr):
    """Lookup cache entry for address"""
    hash_buckets = cache['total_size'] // 8  # Approximate
    # This is simplified - real lookup uses hash function
    hash_table = cache['hash_table']
    
    for i in range(hash_buckets):
        try:
            entry = hash_table[i]
            while entry:
                if int(entry['arm_addr']) == addr:
                    return entry
                entry = entry['next']
        except:
            pass
    
    return None


def disasm_code(addr, length):
    """Disassemble x86 code"""
    try:
        mem = gdb.selected_inferior().read_memory(addr, length)
        disasm = gdb.execute("x/32ib 0x%x" % addr, to_string=True)
        print(disasm)
    except Exception as e:
        print("Cannot disassemble: %s" % str(e))


# Register commands
Arm2x86Command()

print("Arm2x86 GDB Plugin loaded successfully")
print("Type 'arm2x86 help' for available commands")
