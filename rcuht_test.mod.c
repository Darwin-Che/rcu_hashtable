#include <linux/build-salt.h>
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(.gnu.linkonce.this_module) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section(__versions) = {
	{ 0xb3753869, "module_layout" },
	{ 0x5ab5b891, "param_ops_int" },
	{ 0xb0ebce5e, "param_ops_long" },
	{ 0xde4c1a24, "param_ops_charp" },
	{ 0x977f511b, "__mutex_init" },
	{ 0xe2d5255a, "strcmp" },
	{ 0x9e7d6bd0, "__udelay" },
	{ 0xf5cb25c8, "kmem_cache_alloc_trace" },
	{ 0x35216b26, "kmalloc_caches" },
	{ 0x952664c5, "do_exit" },
	{ 0x79aa04a2, "get_random_bytes" },
	{ 0x15ba50a6, "jiffies" },
	{ 0xdecd0b29, "__stack_chk_fail" },
	{ 0x37befc70, "jiffies_to_msecs" },
	{ 0x1000e51, "schedule" },
	{ 0xdbf17652, "_raw_spin_lock" },
	{ 0x64c17a3f, "wake_up_process" },
	{ 0x1b44c663, "current_task" },
	{ 0xc5850110, "printk" },
	{ 0xe8bc695c, "kthread_create_on_node" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0x2ea2c95c, "__x86_indirect_thunk_rax" },
	{ 0x6091797f, "synchronize_rcu" },
	{ 0x19f462ab, "kfree_call_rcu" },
	{ 0x409bcb62, "mutex_unlock" },
	{ 0x2ab7989d, "mutex_lock" },
	{ 0x37a0cba, "kfree" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x36e58bcd, "pv_ops" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "C0A1E9E865A2534E96E2625");
