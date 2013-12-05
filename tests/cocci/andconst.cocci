// Two comparisons of the same expression to different constants,
// connected by a conjunction
// Confidence: Moderate
// Copyright: (C) Diego Liziero
// URL: http://coccinelle.lip6.fr/rules/andconst.html
// Options:

@@ identifier i; constant C1,C2; @@
(
- i == C1 && i == C2
+ i == C1 || i == C2
)

@@ identifier i; constant C1,C2; @@
(
- i != C1 || i != C2
+ i != C1 && i != C2
)
