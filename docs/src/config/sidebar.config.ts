export const SECTION_CATEGORIES = [
  { label: '', ids: ['home'] },
  { label: 'System', ids: ['challenge', 'architecture', 'rules'] },
  { label: 'Operate', ids: ['getting-started', 'performance', 'runtime-tuning', 'ci-cd-pipeline'] },
] as const;

export const SECTION_ORDER = SECTION_CATEGORIES.flatMap(({ ids }) => ids);
