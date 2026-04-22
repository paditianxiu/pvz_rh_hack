import { Typography } from 'antd';
import type { ReactNode } from 'react';

type SettingRowProps = {
  label: ReactNode;
  description?: ReactNode;
  children?: ReactNode;
  className?: string;
  labelClassName?: string;
  descriptionClassName?: string;
};

function joinClassNames(...classNames: Array<string | undefined>): string {
  return classNames.filter(Boolean).join(' ');
}

function SettingRow({
  label,
  description,
  children,
  className,
  labelClassName,
  descriptionClassName,
}: SettingRowProps) {
  return (
    <div className={joinClassNames('setting-row', className)}>
      <div className="setting-info">
        <Typography.Text className={joinClassNames('setting-label', labelClassName)}>
          {label}
        </Typography.Text>
        {description !== undefined && description !== null ? (
          <Typography.Text className={joinClassNames('setting-description', descriptionClassName)}>
            {description}
          </Typography.Text>
        ) : null}
      </div>
      {children}
    </div>
  );
}

export type { SettingRowProps };
export default SettingRow;
