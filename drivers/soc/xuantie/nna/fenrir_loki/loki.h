// SPDX-License-Identifier: GPL-2.0+
/*
 * Fenrir's Loki header
 */
#ifndef LOKI_H
#define LOKI_H

#define DEVICE_NAME "loki_intc"

#define REG_LOKI_EXTERNAL_RESET            (0x0084)
#define REG_LOKI_INTERRUPT_STATUS          (0x0100)
#define REG_LOKI_INTERRUPT_ENABLE          (0x0104)
#define REG_LOKI_INTERRUPT_CLR             (0x010C)
#define REG_LOKI_INTERRUPT_TEST            (0x0110)
#define REG_LOKI_INTERRUPT_TIMEOUT_CLR     (0x0114)
#define REG_LOKI_INTERRUPT_TIMEOUT         (0x0118)

#define REG_LOKI_MEMIF_CACHE_SET           (0x0230)
#define REG_LOKI_MEMIF_PROT_SET            (0x0234)

/* interrupt bits definitions */
#define LOKI_INTERRUPT_MASTER_ENABLE       (1 << 31)
#define LOKI_INTERRUPT_TESTINT             (1 << 30)
#define LOKI_INTERRUPT_DUT0                (1 <<  0)

#define LOKI_INTERRUPT_BASE                 (LOKI_INTERRUPT_MASTER_ENABLE | LOKI_INTERRUPT_TESTINT)

struct loki_intc_drvdata {
    struct irq_domain *domain;
    int irq_num;
};

struct loki_drvdata {
    void __iomem *regbase;
    unsigned int (*readreg32)(struct loki_drvdata *pdata, unsigned long offset);
    void (*writereg32)(struct loki_drvdata *pdata, unsigned long offset, int val);

    struct loki_intc_drvdata intc;
};

int loki_intc_probe(struct platform_device *pdev);
int loki_intc_remove(struct platform_device *pdev);

#endif /* LOKI_H */
