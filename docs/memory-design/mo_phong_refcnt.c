// Mô phỏng thiết kế shared anonymous page + bộ đếm tham chiếu (refcnt)
// Người thực hiện: Đặng Trung Kiên - 20233473 (Tuần 2)
// So sánh 2 cách khi cha và con dùng chung 1 trang vật lý:
//   [A] Chia sẻ trang nhưng KHÔNG có refcnt (kfree như xv6 gốc)
//   [B] Thiết kế đề xuất: cờ PTE_SH + refcnt[] trong kalloc/kfree
// Biên dịch: gcc -Wall -o mo_phong_refcnt mo_phong_refcnt.c && ./mo_phong_refcnt
#include <stdio.h>
#include <string.h>

#define NFRAME   16                 // số trang vật lý mô phỏng
#define NVPAGE   8                  // số trang ảo mỗi tiến trình
#define PGSIZE   4096
#define KERNBASE 0x80000000UL
#define PTE_V  (1 << 0)
#define PTE_R  (1 << 1)
#define PTE_W  (1 << 2)
#define PTE_U  (1 << 4)
#define PTE_SH (1 << 8)             // bit RSW 8: trang dùng chung

typedef struct { int flags; int frame; } pte_t;
typedef struct { const char *name; pte_t pt[NVPAGE]; int sz; } proc_t;

static int mem[NFRAME];             // nội dung 1 ô nhớ đại diện cho mỗi trang
static int is_free[NFRAME];
static int refcnt[NFRAME];
static int use_refcnt;              // 0 = cách A, 1 = cách B
static int errors;

static unsigned long PA(int f) { return KERNBASE + 0x100000UL + (unsigned long)f * PGSIZE; }
static int nfree(void) { int n = 0; for (int i = 0; i < NFRAME; i++) n += is_free[i]; return n; }

static int kalloc(void) {
  for (int i = 0; i < NFRAME; i++)
    if (is_free[i]) { is_free[i] = 0; refcnt[i] = 1; mem[i] = 0; return i; }
  return -1;
}
static void kref_inc(int f) { refcnt[f]++; }
static void kfree(int f) {
  if (use_refcnt) {
    if (refcnt[f] <= 0) { printf("      !! panic: kfree: double free (PA 0x%lx)\n", PA(f)); errors++; return; }
    if (--refcnt[f] > 0) { printf("      kfree(0x%lx): refcnt -> %d, GIỮ trang\n", PA(f), refcnt[f]); return; }
    printf("      kfree(0x%lx): refcnt -> 0, trả về freelist\n", PA(f));
  } else {
    if (is_free[f]) { printf("      !! DOUBLE FREE: trang 0x%lx đã nằm trong freelist -> freelist hỏng\n", PA(f)); errors++; return; }
  }
  is_free[f] = 1; mem[f] = 0x01010101;   // xv6 ghi rác vào trang đã giải phóng
}

static void uvmalloc(proc_t *p, int n) {
  for (int i = 0; i < n; i++) { int f = kalloc(); p->pt[p->sz] = (pte_t){PTE_V|PTE_R|PTE_W|PTE_U, f}; p->sz++; }
}
static int shmalloc(proc_t *p) {
  int f = kalloc(); int va = p->sz;
  p->pt[va] = (pte_t){PTE_V|PTE_R|PTE_W|PTE_U|PTE_SH, f}; p->sz++;
  printf("   %s: shmalloc(1) -> va=0x%x, PA=0x%lx, cờ PTE_SH\n", p->name, va * PGSIZE, PA(f));
  return va;
}
static void fork_copy(proc_t *old, proc_t *new) {        // uvmcopy()
  new->sz = old->sz;
  for (int i = 0; i < old->sz; i++) {
    pte_t *pte = &old->pt[i];
    if (pte->flags & PTE_SH) {                           // NHÁNH MỚI
      new->pt[i] = *pte;
      if (use_refcnt) kref_inc(pte->frame);
      printf("      va=0x%x: PTE_SH -> dùng CHUNG PA 0x%lx%s\n", i * PGSIZE, PA(pte->frame),
             use_refcnt ? "" : " (không tăng bộ đếm)");
      if (use_refcnt) printf("                 refcnt = %d\n", refcnt[pte->frame]);
    } else {                                             // như cũ: sao chép
      int f = kalloc(); mem[f] = mem[pte->frame];
      new->pt[i] = (pte_t){pte->flags, f};
    }
  }
}
static void proc_exit(proc_t *p) {                       // uvmfree -> uvmunmap -> kfree
  printf("   %s: exit() -> uvmunmap(do_free=1)\n", p->name);
  for (int i = 0; i < p->sz; i++) kfree(p->pt[i].frame);
  p->sz = 0;
}
static int *vaddr(proc_t *p, int va) { return &mem[p->pt[va].frame]; }

static void run(int mode) {
  use_refcnt = mode; errors = 0;
  for (int i = 0; i < NFRAME; i++) { is_free[i] = 1; refcnt[i] = 0; }
  proc_t cha = {.name = "CHA"}, con = {.name = "CON"};
  printf("\n==================================================================\n");
  printf(mode ? " [B] THIẾT KẾ ĐỀ XUẤT: PTE_SH + refcnt\n" : " [A] CHIA SẺ TRANG NHƯNG KHÔNG CÓ refcnt (kfree xv6 gốc)\n");
  printf("==================================================================\n");
  int free0 = nfree();
  printf(" Trang trống ban đầu: %d\n", free0);
  uvmalloc(&cha, 3);
  printf("   CHA: cấp 3 trang riêng (text, stack, heap)\n");
  int sh = shmalloc(&cha);
  *vaddr(&cha, 2) = 7;                                    // biến riêng ở heap
  printf("   CHA: fork()\n"); fork_copy(&cha, &con);
  printf("   futex_key(CHA, va=0x%x) = 0x%lx | futex_key(CON, va=0x%x) = 0x%lx -> %s\n",
         sh * PGSIZE, PA(cha.pt[sh].frame), sh * PGSIZE, PA(con.pt[sh].frame),
         cha.pt[sh].frame == con.pt[sh].frame ? "CÙNG KHÓA ✔" : "KHÁC KHÓA ✘");
  *vaddr(&con, sh) = 42; *vaddr(&con, 2) = 99;
  printf("   CON: ghi biến chung = 42, biến riêng = 99\n");
  printf("   CHA: đọc biến chung = %d (mong đợi 42), biến riêng = %d (mong đợi 7)\n",
         *vaddr(&cha, sh), *vaddr(&cha, 2));
  proc_exit(&con);
  int v = *vaddr(&cha, sh);
  if (v == 42) printf("   CHA: sau khi CON thoát, đọc biến chung = %d -> vẫn đúng ✔\n", v);
  else printf("   CHA: sau khi CON thoát, đọc biến chung = 0x%x (rác) -> ĐÃ BỊ GIẢI PHÓNG (use-after-free) ✘\n", v);
  if (v != 42) errors++;
  proc_exit(&cha);
  int free1 = nfree();
  printf(" Trang trống cuối cùng: %d (ban đầu %d) -> %s\n", free1, free0,
         free1 == free0 ? "không rò ✔" : "SAI ✘");
  if (free1 != free0) errors++;
  printf(" KẾT QUẢ [%c]: %s\n", mode ? 'B' : 'A', errors ? "LỖI" : "ĐÚNG - không double-free, không use-after-free");
}

int main(void) {
  printf("Mô phỏng bộ đếm tham chiếu trang vật lý cho xv6 (Đặng Trung Kiên - 20233473)\n");
  run(0);
  run(1);
  return 0;
}
