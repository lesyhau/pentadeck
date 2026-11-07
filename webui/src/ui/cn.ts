/** Joins the class names that are set. */
export const cn = (...names: (string | false | null | undefined)[]) => names.filter(Boolean).join(' ')
